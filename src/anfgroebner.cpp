/*****************************************************************************
Copyright (C) 2026  Mate Soos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
***********************************************/

// gb-cone: degree-bounded Groebner bases of cones of small equations.
//
// XL multiplies equations by variables once and eliminates; a Groebner basis
// computation keeps reducing S-polynomials until closure, and finds the
// low-degree consequences (in particular linear equations) that XL misses.
// A full basis of the whole system is hopeless, so the rule works on windows:
// a small equation together with the small equations sharing variables with
// it, at most gbWindow of them, with S-polynomials above gbDeg dropped
// (Buchberger with a degree bound, using BRiAl's GroebnerStrategy). Only the
// linear members of the resulting bases are added to the system; they are
// consequences of the window's equations, so this is sound.

#include <iomanip>
#include <polybori/groebner/GroebnerStrategy.h>
#include <unordered_map>
#include <limits>

#include "anf.hpp"
#include "boolf4.hpp"
#include "time_mem.h"

using std::cout;
using std::endl;
using namespace BLib;
using polybori::groebner::GroebnerStrategy;

size_t ANF::groebner_windows()
{
    SimpStatsScope scope(*this, "gb-cone");
    const double myTime = cpuTime();
    // a deterministic work budget: runs are reproducible, unlike time limits
    uint64_t steps_left = config.gbSteps;

    // A small system (few free variables) is one cone: its complete
    // Groebner basis in a degree ordering solves e.g. random MQ systems
    // with up to ~28 variables outright, where SAT solvers time out.
    size_t max_vars = config.gbMaxVars, max_len = config.gbMaxLen, max_window = config.gbWindow;
    bool whole = false;
    if (config.gbFull && numActiveVars() <= config.gbWholeVars) {
        whole = true;
        max_vars = std::max<size_t>(max_vars, config.gbWholeVars);
        max_len = std::numeric_limits<size_t>::max();
        max_window = std::numeric_limits<size_t>::max();
    }

    // the small equations and, per variable, which of them contain it
    vector<size_t> small;
    vector<vector<size_t> > by_var(ring->nVariables());
    for (size_t i = 0; i < eqs.size(); i++) {
        if (eq_len[i] == 0 || eq_len[i] > max_len) continue;
        if (nVarsOf(i) > max_vars) continue;
        small.push_back(i);
        forEachVar(i, [&](uint32_t v) {
            if (by_var[v].empty() || by_var[v].back() != i) by_var[v].push_back(i);
        });
    }
    // seeds in order of increasing size: the smallest equations first
    std::stable_sort(small.begin(), small.end(), [&](size_t a, size_t b) { return eq_len[a] < eq_len[b]; });

    vector<char> covered(eqs.size(), 0);
    vector<uint32_t> in_cone(ring->nVariables(), 0); // var -> cone stamp
    uint32_t stamp = 0;
    vector<BoolePolynomial> facts;
    size_t windows = 0, spolys = 0, cand_facts = 0;
    bool timeout = false;
    for (const size_t seed : small) {
        if (covered[seed]) continue;
        if (steps_left == 0) { timeout = true; break; }
        // The cone: the seed and, repeatedly, the small equation sharing
        // the most variables with what is already in, as long as the union
        // of variables stays within gbMaxVars (so the Groebner basis is of a
        // small system that can be computed fully).
        stamp++;
        vector<size_t> window(1, seed);
        VarVec cvars = varsVecOf(seed);
        for (const uint32_t v : cvars) in_cone[v] = stamp;
        vector<char> in_window(eqs.size(), 0); // could be a stamp too, sizes are small
        in_window[seed] = 1;
        while (window.size() < max_window) {
            size_t best = eqs.size(), best_shared = 0, best_new = 0;
            for (const uint32_t v : cvars) {
                for (const size_t j : by_var[v]) {
                    if (in_window[j]) continue;
                    size_t shared = 0, fresh = 0;
                    VarVec jv = varsVecOf(j);
                    for (const uint32_t w : jv) (in_cone[w] == stamp ? shared : fresh)++;
                    if (cvars.size() + fresh > max_vars) continue;
                    if (shared > best_shared || (shared == best_shared && best != eqs.size() && fresh < best_new)) {
                        best = j; best_shared = shared; best_new = fresh;
                    }
                }
            }
            if (best == eqs.size()) break;
            in_window[best] = 1;
            window.push_back(best);
            for (const uint32_t w : varsVecOf(best)) {
                if (in_cone[w] != stamp) { in_cone[w] = stamp; cvars.push_back(w); }
            }
        }
        for (const size_t j : window) covered[j] = 1;
        if (window.size() < 2) continue; // nothing to combine
        {
            // Gaussian elimination (lin-gauss) already handles a cone of
            // linear equations; and a cone made of exactly the equations of
            // an earlier run cannot yield anything new
            bool nonlinear = false;
            uint64_t sig = 1469598103934665603ULL;
            vector<uint64_t> hs;
            for (const size_t j : window) {
                if (degOf(j) > 1) nonlinear = true;
                hs.push_back(eq(j).stableHash());
            }
            if (!nonlinear) continue;
            std::sort(hs.begin(), hs.end());
            for (const uint64_t h : hs) sig ^= h + 0x9e3779b97f4a7c15ULL + (sig << 6) + (sig >> 2);
            if (!gb_seen.insert(sig).second) continue;
        }
        windows++;

        if (config.gbEngine == 1 && (config.gbFull == 1 || (config.gbFull == 2 && whole)) && cvars.size() <= 64) {
            // Bosphorus's own matrix-F4 engine over the Boolean ring
            // (boolf4.cpp): dense GF(2) linear algebra with M4RI on the
            // critical pairs of one degree at a time
            if (steps_left < window.size()) { timeout = true; break; }
            steps_left -= window.size();
            std::sort(cvars.begin(), cvars.end());
            std::unordered_map<uint32_t, uint32_t> local;
            for (uint32_t k = 0; k < cvars.size(); k++) local[cvars[k]] = k;
            BoolF4::Options fopt;
            fopt.maxDeg = whole ? 64 : config.gbDeg;
            fopt.maxRows = config.gbSteps;
            fopt.maxCells = config.gbMaxCells;
            fopt.tailReduce = config.gbTailReduce;
            fopt.verbosity = config.verbosity;
            BoolF4 f4(cvars.size(), fopt);
            for (const size_t j : window) {
                BoolF4::Poly q;
                for (const BooleMonomial& m : eq(j)) {
                    BoolF4::Mon mask = 0;
                    for (const uint32_t v : m) mask |= (BoolF4::Mon)1 << local[v];
                    q.push_back(mask);
                }
                f4.add(q);
            }
            const vector<BoolF4::Poly> basis = f4.run();
            spolys += f4.stats().rows;
            if (f4.stats().budget_exhausted) timeout = true;
            if (config.verbosity >= 1 && whole) {
                cout << "c [f4] whole system: steps " << f4.stats().steps << " rows " << f4.stats().rows
                     << " max cols " << f4.stats().cols_max << " max degree " << f4.stats().max_deg
                     << " pairs-to-zero " << f4.stats().zero_reductions
                     << " basis " << basis.size() << (f4.stats().budget_exhausted ? " (budget exhausted)" : "") << endl;
            }
            for (const BoolF4::Poly& cg : basis) {
                if (cg.size() == 1 && cg[0] == 0) { facts.push_back(BoolePolynomial(true, *ring)); break; }
                uint32_t d = 0;
                for (const BoolF4::Mon m : cg) d = std::max<uint32_t>(d, BoolF4::deg(m));
                if (d > config.gbFactDeg || cg.size() > config.gbFactLen) continue;
                cand_facts++;
                BoolePolynomial g(*ring);
                for (const BoolF4::Mon m : cg) {
                    BooleMonomial gm(*ring);
                    for (uint32_t k = 0; k < cvars.size(); k++) {
                        if ((m >> k) & 1) gm *= ring->variable(cvars[k]);
                    }
                    g += gm;
                }
                if (eqs_hash.find(g.hash()) == eqs_hash.end()) facts.push_back(g);
            }
            if (timeout) break;
            continue;
        }
        if (config.gbFull == 1 || (config.gbFull == 2 && whole)) {
            // BRiAl's complete algorithm (the engine behind Sage's
            // groebner_basis for Boolean rings, with its F4-style dense
            // reduction steps): exact, no degree bound; the cones are small
            // (at most gbMaxVars variables) so the work is bounded by size.
            // It runs in a ring of its own over the cone's variables with a
            // degree ordering (the main ring is lexicographic, which makes
            // Groebner bases far more expensive), and the results are
            // mapped back. The step budget is charged one unit per generator.
            if (steps_left < window.size()) { timeout = true; break; }
            steps_left -= window.size();
            std::sort(cvars.begin(), cvars.end());
            std::unordered_map<uint32_t, uint32_t> local; // main var -> cone var
            for (uint32_t k = 0; k < cvars.size(); k++) local[cvars[k]] = k;
            // one ring (one ZDD manager) per cone size, reused across cones
            auto rit = gb_rings.find(cvars.size());
            if (rit == gb_rings.end()) {
                rit = gb_rings.emplace(cvars.size(), BoolePolyRing(cvars.size(), COrderEnums::dp_asc)).first;
            }
            BoolePolyRing& cring = rit->second;
            GroebnerStrategy cstrat(cring);
            // BRiAl's recursive "implication" bases for split generators
            // dominate the time on small cones (57% of a run) and help
            // little there, but the whole-system basis of an MQ-like system
            // needs them (n = 24: 16 s with, > 600 s without)
            cstrat.optAllowRecursion = config.gbRecursion == 1 || (config.gbRecursion == 2 && whole);
            for (const size_t j : window) {
                BoolePolynomial q(cring);
                for (const BooleMonomial& m : eq(j)) {
                    BooleMonomial cm(cring);
                    for (const uint32_t v : m) cm *= cring.variable(local[v]);
                    q += cm;
                }
                cstrat.addAsYouWish(q);
            }
            cstrat.symmGB_F2();
            spolys += cstrat.generators.size();
            if (cstrat.containsOne()) {
                facts.push_back(BoolePolynomial(true, *ring));
                break;
            }
            for (const BoolePolynomial& cg : cstrat.minimalizeAndTailReduce()) {
                if (cg.isConstant()) { if (cg.isOne()) facts.push_back(BoolePolynomial(true, *ring)); continue; }
                // short members only, linear ones included: long linear
                // consequences join later cones (they are short enough for
                // gbMaxLen) and crowd out the structure the cones should
                // capture; with the length bound the three-round ascon
                // instances are solved by the cones alone
                if ((uint32_t)cg.deg() > config.gbFactDeg || cg.length() > config.gbFactLen) continue;
                cand_facts++;
                BoolePolynomial g(*ring);
                for (const BooleMonomial& m : cg) {
                    BooleMonomial gm(*ring);
                    for (const uint32_t v : m) gm *= ring->variable(cvars[v]);
                    g += gm;
                }
                if (eqs_hash.find(g.hash()) == eqs_hash.end()) facts.push_back(g);
            }
            continue;
        }
        // Small cones: the degree-bounded Buchberger loop in the main ring
        // (lexicographic: its bases eliminate variables, which is what
        // makes the three-round ascon instances collapse) and, with
        // --gbfull 2, the complete degree-ordered basis as well: the two
        // orderings find different short consequences.
        if (config.gbFull == 2) {
            std::sort(cvars.begin(), cvars.end());
            std::unordered_map<uint32_t, uint32_t> local;
            for (uint32_t k = 0; k < cvars.size(); k++) local[cvars[k]] = k;
            auto rit = gb_rings.find(cvars.size());
            if (rit == gb_rings.end()) {
                rit = gb_rings.emplace(cvars.size(), BoolePolyRing(cvars.size(), COrderEnums::dp_asc)).first;
            }
            BoolePolyRing& cring = rit->second;
            GroebnerStrategy cstrat(cring);
            cstrat.optAllowRecursion = config.gbRecursion == 1;
            for (const size_t j : window) {
                BoolePolynomial q(cring);
                for (const BooleMonomial& m : eq(j)) {
                    BooleMonomial cm(cring);
                    for (const uint32_t v : m) cm *= cring.variable(local[v]);
                    q += cm;
                }
                cstrat.addAsYouWish(q);
            }
            cstrat.symmGB_F2();
            spolys += cstrat.generators.size();
            if (cstrat.containsOne()) {
                facts.push_back(BoolePolynomial(true, *ring));
                break;
            }
            for (const BoolePolynomial& cg : cstrat.minimalizeAndTailReduce()) {
                if (cg.isConstant()) { if (cg.isOne()) facts.push_back(BoolePolynomial(true, *ring)); continue; }
                if ((uint32_t)cg.deg() > config.gbFactDeg || cg.length() > config.gbFactLen) continue;
                cand_facts++;
                BoolePolynomial g(*ring);
                for (const BooleMonomial& m : cg) {
                    BooleMonomial gm(*ring);
                    for (const uint32_t v : m) gm *= ring->variable(cvars[v]);
                    g += gm;
                }
                if (eqs_hash.find(g.hash()) == eqs_hash.end()) facts.push_back(g);
            }
        }
        GroebnerStrategy strat(*ring);
        strat.optLazy = false;
        for (const size_t j : window) strat.addAsYouWish(eq(j));
        {
            // Buchberger with a degree bound
            while (!strat.pairs.pairSetEmpty()) {
                if (steps_left == 0) { timeout = true; break; }
                steps_left--;
                BoolePolynomial p = strat.nextSpoly();
                p = strat.nf(p);
                spolys++;
                if (p.isZero()) continue;
                if (p.isOne()) { facts.push_back(p); break; }
                if ((uint32_t)p.deg() > config.gbDeg) continue;
                strat.addAsYouWish(p);
            }
        }
        if (strat.containsOne()) {
            facts.push_back(BoolePolynomial(true, *ring));
            break;
        }
        for (const BoolePolynomial& g : strat.minimalizeAndTailReduce()) {
            if (g.isConstant()) { if (g.isOne()) facts.push_back(g); continue; }
            // short consequences only: units, equivalences, short XORs and
            // small nonlinear relations (e.g. the implicit quadratic
            // relations of an S-box); long ones only bloat the system
            if ((uint32_t)g.deg() <= config.gbFactDeg && g.length() <= config.gbFactLen) {
                cand_facts++;
                if (eqs_hash.find(g.hash()) == eqs_hash.end()) facts.push_back(g);
            }
        }
        if (timeout) break;
    }

    // linear facts that are combinations of the linear equations already in
    // the system are dropped (lin-gauss would delete them again next round
    // and the cone would find them again: churn)
    const size_t added = add_linearly_new_facts(facts, false);
    if (added > 0) {
        if (!propagate()) setNOTOK();
    }
    if (config.verbosity >= 1) {
        cout << "c [gb-cone] small eqs " << small.size() << " cones " << windows
             << " S-polys " << spolys << " short GB members " << cand_facts
             << " new " << added << (timeout ? " (step budget exhausted)" : "") << " T: "
             << std::fixed << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return added;
}
