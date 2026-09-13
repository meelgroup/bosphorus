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

// In-place rewrite rules on the ANF. Unlike XL/ElimLin/SAT, which work on a
// copy of the system and only feed back linear facts, these rules change the
// equations of the ANF itself. Every rule replaces an equation p by p + q
// where q is a multiple of another equation still present in the system, or
// adds a fact implied by a single equation, so the solution set over all
// variables never changes.

#include <algorithm>
#include <iomanip>

#include "anf.hpp"
#include "time_mem.h"

using std::cout;
using std::endl;
using std::unordered_map;
using namespace BLib;

namespace {

// Degree-lexicographic monomial order: larger degree first, then the
// lexicographic order of PolyBoRi (x0 > x1 > ...). Reduction with this order
// can never increase the degree of a polynomial and always terminates.
bool deglex_less(const BooleMonomial& a, const BooleMonomial& b)
{
    if (a.deg() != b.deg()) return a.deg() < b.deg();
    auto ia = a.begin();
    auto ib = b.begin();
    for (; ia != a.end() && ib != b.end(); ++ia, ++ib) {
        if (*ia != *ib) return *ia > *ib;
    }
    return false;
}

BooleMonomial deglex_lead(const BoolePolynomial& poly)
{
    BooleMonomial best(poly.ring());
    bool first = true;
    for (const BooleMonomial& t : poly) {
        if (first || deglex_less(best, t)) {
            best = t;
            first = false;
        }
    }
    return best;
}

}

///////////////////////////////////////////////////////////////////////////////
// Common plumbing
///////////////////////////////////////////////////////////////////////////////

bool ANF::rewrite_eq(size_t idx, const BoolePolynomial& newpoly,
                     unordered_set<uint32_t>& updatedVars,
                     vector<size_t>& empty_equations)
{
    if (!updateEquations(idx, newpoly, empty_equations)) {
        return false; // UNSAT
    }
    if (!eqs[idx].isConstant()) {
        // A rewritten equation may have become x, x+1, x+y(+1) or m+1:
        // hand it to the replacer like propagation does.
        check_if_need_update(eqs[idx], updatedVars);
    }
    return true;
}

bool ANF::finish_rewrites(unordered_set<uint32_t>& updatedVars,
                          vector<size_t>& empty_equations)
{
    // Substitutes what the replacer learnt into all affected equations and
    // removes the equations that became empty.
    return propagate_iteratively(updatedVars, empty_equations);
}

///////////////////////////////////////////////////////////////////////////////
// binom-red: reduction modulo monomial and binomial equations
//
// An equation with at most `config.binomRedLen` terms (by default 2) is used
// as a rewrite rule "lead -> rest", where lead is its deglex-leading monomial:
//   m = 0          every monomial divisible by m is deleted
//   m1 + m2 = 0    every monomial t divisible by m1 becomes (t/m1)*m2
// e.g. x*y + x = 0 (x implies y) turns x*y*z into x*z in every other equation,
// and x*y + z = 0 (a definition) lowers the degree of every monomial that
// contains x*y. This is one step of a Groebner-basis interreduction,
// restricted to rules that can never blow up a polynomial.
///////////////////////////////////////////////////////////////////////////////

namespace {
struct RedRule {
    size_t eq_idx;        // which equation it came from (never reduce it by itself)
    BooleMonomial lead;
    BoolePolynomial poly; // the full equation, always kept current
    bool alive;
    RedRule(size_t i, const BooleMonomial& l, const BoolePolynomial& p)
        : eq_idx(i), lead(l), poly(p), alive(true) {}
};
}

size_t ANF::reduce_by_short_polys()
{
    SimpStatsScope scope(*this, "binom-red");
    const double myTime = cpuTime();
    const size_t max_len = config.binomRedLen;
    size_t num_rewrites = 0;
    size_t num_monom_rewrites = 0;

    auto is_rule = [&](const BoolePolynomial& p) {
        return !p.isConstant() && p.length() <= max_len && p.deg() >= 1;
    };

    for (unsigned round = 0; round < 20; round++) {
        vector<RedRule> rules;
        vector<vector<size_t> > rules_by_var(ring->nVariables());
        vector<size_t> rule_of_eq(eqs.size(), std::numeric_limits<size_t>::max());

        auto add_rule = [&](size_t eq_idx) {
            const BoolePolynomial& p = eqs[eq_idx];
            const BooleMonomial lead = deglex_lead(p);
            if (lead.deg() == 0) return;
            const size_t id = rules.size();
            rules.emplace_back(eq_idx, lead, p);
            // indexed by the smallest variable of the lead: if lead | t then
            // that variable is in t, so looking up every variable of t finds
            // every candidate exactly once
            rules_by_var[*lead.begin()].push_back(id);
            rule_of_eq[eq_idx] = id;
        };
        for (size_t i = 0; i < eqs.size(); i++) {
            if (is_rule(eqs[i])) add_rule(i);
        }
        if (rules.empty()) break;

        // Finds a live rule (not from equation eq_idx) whose lead divides t.
        auto find_rule = [&](const BooleMonomial& t, size_t eq_idx) -> const RedRule* {
            for (const uint32_t v : t) {
                for (const size_t id : rules_by_var[v]) {
                    const RedRule& r = rules[id];
                    if (!r.alive || r.eq_idx == eq_idx) continue;
                    if (t.reducibleBy(r.lead)) return &r;
                }
            }
            return nullptr;
        };

        unordered_set<uint32_t> updatedVars;
        vector<size_t> empty_equations;
        size_t round_rewrites = 0;
        for (size_t i = 0; i < eqs.size(); i++) {
            if (eqs[i].isConstant()) continue;
            BoolePolynomial poly = eqs[i];
            bool changed = false;
            // Reduce until no monomial is divisible by any rule's lead. Each
            // pass adds multiples of rules, so all the reducible monomials
            // found in one pass can be rewritten together.
            for (unsigned pass = 0; pass < 100; pass++) {
                BoolePolynomial delta(*ring);
                size_t hits = 0;
                for (const BooleMonomial& t : poly) {
                    if (t.deg() == 0) continue;
                    const RedRule* r = find_rule(t, i);
                    if (r == nullptr) continue;
                    // t = (t/lead)*lead, so p + (t/lead)*rule kills t and
                    // puts (t/lead)*rest in its place
                    delta += (t / r->lead) * r->poly;
                    hits++;
                }
                if (hits == 0) break;
                poly += delta;
                num_monom_rewrites += hits;
                changed = true;
                if (poly.isConstant()) break;
            }
            if (!changed) continue;

            round_rewrites++;
            if (config.verbosity >= 5) {
                cout << "c [binom-red] " << eqs[i] << "  -->  " << poly << endl;
            }
            // Keep the rule table exact: a rule whose equation changed must
            // not be used any more in its old form (that would be unsound),
            // so retire it and add the new equation as a rule if it still is
            // short enough.
            if (rule_of_eq[i] != std::numeric_limits<size_t>::max()) {
                rules[rule_of_eq[i]].alive = false;
                rule_of_eq[i] = std::numeric_limits<size_t>::max();
            }
            if (!rewrite_eq(i, poly, updatedVars, empty_equations)) {
                return num_rewrites + round_rewrites; // UNSAT, replacer set
            }
            if (is_rule(eqs[i])) add_rule(i);
        }
        num_rewrites += round_rewrites;
        if (!finish_rewrites(updatedVars, empty_equations)) break;
        if (round_rewrites == 0) break;
        if (cpuTime() > config.maxTime) break;
    }

    if (config.verbosity >= 1) {
        cout << "c [binom-red] rewrote " << num_rewrites << " eqs ("
             << num_monom_rewrites << " monomials) T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_rewrites;
}

///////////////////////////////////////////////////////////////////////////////
// Driver: run the in-place rules until nothing changes
///////////////////////////////////////////////////////////////////////////////

size_t ANF::rewrite_inplace()
{
    size_t total = 0;
    for (unsigned round = 0; round < config.rewriteRounds; round++) {
        if (!getOK()) break;
        size_t changes = 0;
        if (config.doBinomRed) changes += reduce_by_short_polys();
        if (!getOK()) break;
        total += changes;
        if (changes == 0) break;
        if (cpuTime() > config.maxTime) break;
    }
    return total;
}
