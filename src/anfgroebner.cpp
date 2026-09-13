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

// gb-window: degree-bounded Groebner bases of small windows of the system.
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
    SimpStatsScope scope(*this, "gb-window");
    const double myTime = cpuTime();
    // a deterministic work budget: runs are reproducible, unlike time limits
    uint64_t steps_left = config.gbSteps;

    // the small equations and, per variable, which of them contain it
    vector<size_t> small;
    vector<vector<size_t> > by_var(ring->nVariables());
    for (size_t i = 0; i < eqs.size(); i++) {
        if (eq_len[i] == 0 || eq_len[i] > config.gbMaxLen) continue;
        small.push_back(i);
        forEachVar(i, [&](uint32_t v) {
            if (by_var[v].empty() || by_var[v].back() != i) by_var[v].push_back(i);
        });
    }

    vector<char> covered(eqs.size(), 0);
    vector<BoolePolynomial> facts;
    size_t windows = 0, spolys = 0;
    bool timeout = false;
    for (const size_t seed : small) {
        if (covered[seed]) continue;
        if (steps_left == 0) { timeout = true; break; }

        // the window: the seed and the small equations sharing a variable
        vector<size_t> window;
        window.push_back(seed);
        forEachVar(seed, [&](uint32_t v) {
            for (const size_t j : by_var[v]) {
                if (window.size() >= config.gbWindow) return;
                if (j == seed || std::find(window.begin(), window.end(), j) != window.end()) continue;
                window.push_back(j);
            }
        });
        for (const size_t j : window) covered[j] = 1;
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
            // only short linear consequences: they propagate (units,
            // equivalences, short XORs); long ones only bloat the system
            if (g.deg() <= 1 && g.nUsedVariables() <= config.gbMaxFactVars) facts.push_back(g);
        }
        if (timeout) break;
    }

    size_t added = 0;
    for (const BoolePolynomial& f : facts) added += addBoolePolynomial(f);
    if (added > 0) {
        if (!propagate()) setNOTOK();
    }
    if (config.verbosity >= 1) {
        cout << "c [gb-window] small eqs " << small.size() << " windows " << windows
             << " S-polys " << spolys << " linear facts " << facts.size()
             << " new " << added << (timeout ? " (step budget exhausted)" : "") << " T: "
             << std::fixed << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return added;
}
