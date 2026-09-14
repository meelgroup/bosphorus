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

#include "anf.hpp"
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

    // the small equations and, per variable, which of them contain it
    vector<size_t> small;
    vector<vector<size_t> > by_var(ring->nVariables());
    for (size_t i = 0; i < eqs.size(); i++) {
        if (eq_len[i] == 0 || eq_len[i] > config.gbMaxLen) continue;
        if (nVarsOf(i) > config.gbMaxVars) continue;
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
        while (window.size() < config.gbWindow) {
            size_t best = eqs.size(), best_shared = 0, best_new = 0;
            for (const uint32_t v : cvars) {
                for (const size_t j : by_var[v]) {
                    if (in_window[j]) continue;
                    size_t shared = 0, fresh = 0;
                    VarVec jv = varsVecOf(j);
                    for (const uint32_t w : jv) (in_cone[w] == stamp ? shared : fresh)++;
                    if (cvars.size() + fresh > config.gbMaxVars) continue;
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
        windows++;

        GroebnerStrategy strat(*ring);
        strat.optLazy = false;
        for (const size_t j : window) strat.addAsYouWish(eq(j));
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

    size_t added = 0;
    for (const BoolePolynomial& f : facts) added += addBoolePolynomial(f);
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
