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
#include <unordered_map>
#include <unordered_set>
#include <iomanip>

#include "anf.hpp"
#include "linfactor.hpp"
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

bool ANF::breaks_product(const BoolePolynomial& from, const BoolePolynomial& to) const
{
    if (keep_factor != 1) return false;
    if (to.isConstant() || to.deg() <= 1) return false;
    vector<Lineral> f;
    if (!factor_into_linerals(from, f) || f.size() < 2) return false;
    return !(factor_into_linerals(to, f) && f.size() >= 2);
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

    auto is_rule = [&](size_t i) {
        if (eq_len[i] > max_len) return false;
        const BoolePolynomial& p = eq(i);
        return !p.isConstant() && p.deg() >= 1;
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
            if (keep_factor == 1 && isProduct(i)) continue;
            if (is_rule(i)) add_rule(i);
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
            // product-preserving mode: products of linear factors are left alone (a
            // reduction would almost never keep the product form, and
            // trying costs a pass over millions of monomials)
            if (keep_factor == 1 && (isProduct(i) || degOf(i) >= 2)) continue;
            if (eq(i).isConstant()) continue;
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
            if (breaks_product(eqs[i], poly)) continue;

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
            if (is_rule(i)) add_rule(i);
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
// poly-shorten: additive shortening by another equation
//
// If two equations f and p share more than half of the terms of f, then
// p + f has fewer terms than p, so p is replaced by p + f. For linear
// equations this is the classic XOR shortening; for nonlinear ones it also
// re-uses definitions: with f = y + x1*x2 + x3 present, an equation containing
// x1*x2 + x3 + ... is rewritten to y + ... . Only f with deg(f) <= deg(p) are
// used so an XOR is never turned into a nonlinear equation.
///////////////////////////////////////////////////////////////////////////////

size_t ANF::shorten_polys()
{
    SimpStatsScope scope(*this, "poly-shorten");
    const double myTime = cpuTime();
    typedef BooleMonomial::hash_type mhash;
    size_t num_rewrites = 0;

    // monomial -> equations containing it (the constant 1 is a monomial too).
    // NOTE: BooleMonomial::hash() is the address of the ZDD node, and the
    // monomials produced while iterating a polynomial are temporaries whose
    // nodes get recycled, so it cannot key a map. stableHash() is structural.
    // In product-preserving mode only the linear equations take part.
    unordered_map<mhash, vector<size_t> > occ_m;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (keep_factor == 1 && (isProduct(i) || degOf(i) >= 2)) continue;
        for (const BooleMonomial& t : eq(i)) {
            occ_m[t.stableHash()].push_back(i);
        }
    }
    auto occ_remove = [&](const BooleMonomial& t, size_t idx) {
        vector<size_t>& v = occ_m[t.stableHash()];
        auto it = std::find(v.begin(), v.end(), idx);
        if (it != v.end()) {
            *it = v.back();
            v.pop_back();
        }
    };

    // shortest equations first: they are the most useful rules
    vector<size_t> order(eqs.size());
    for (size_t i = 0; i < order.size(); i++) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return eq_len[a] < eq_len[b];
    });

    vector<uint32_t> cnt(eqs.size(), 0);
    vector<size_t> touched;
    unordered_set<uint32_t> updatedVars;
    vector<size_t> empty_equations;
    // a monomial in this many equations is too common to be worth counting
    const size_t occ_cap = config.shortenOccCap;
    int64_t budget = config.shortenBudget;

    for (const size_t f_idx : order) {
        if (budget < 0 || cpuTime() > config.maxTime) break;
        if (keep_factor == 1 && (isProduct(f_idx) || degOf(f_idx) >= 2)) continue;
        const BoolePolynomial& f = eq(f_idx);
        if (f.isConstant()) continue;
        const size_t f_len = eq_len[f_idx];
        const int f_deg = f.deg();

        touched.clear();
        for (const BooleMonomial& t : f) {
            auto it = occ_m.find(t.stableHash());
            if (it == occ_m.end() || it->second.size() > occ_cap) continue;
            budget -= it->second.size();
            for (const size_t p : it->second) {
                if (p == f_idx) continue;
                if (cnt[p]++ == 0) touched.push_back(p);
            }
        }

        for (const size_t p : touched) {
            const uint32_t c = cnt[p];
            cnt[p] = 0;
            if (2 * c <= f_len) continue;
            if (keep_factor == 1 && isProduct(p)) continue;
            if (eq(p).isConstant()) continue;
            if (eqs[p].deg() < f_deg) continue;
            if (eq_len[p] < f_len) continue; // then p is the rule for f, not vice versa

            const BoolePolynomial newp = eqs[p] + f;
            if (newp.length() >= eq_len[p]) {
                // cannot happen with an exact index; never make things worse
                continue;
            }
            if (breaks_product(eqs[p], newp)) continue;
            if (config.verbosity >= 5) {
                cout << "c [poly-shorten] " << eqs[p] << "  -->  " << newp
                     << "  (by " << f << ")" << endl;
            }
            // keep the index exact for the terms that moved
            const BooleSet pset = eqs[p].set();
            for (const BooleMonomial& t : f) {
                if (pset.owns(t)) occ_remove(t, p);
                else occ_m[t.stableHash()].push_back(p);
            }
            num_rewrites++;
            if (!rewrite_eq(p, newp, updatedVars, empty_equations)) {
                return num_rewrites; // UNSAT
            }
            if (eqs[p].isConstant()) {
                // became empty (duplicate or 0): drop its index entries
                for (const BooleMonomial& t : newp) occ_remove(t, p);
            }
        }
    }
    finish_rewrites(updatedVars, empty_equations);

    if (config.verbosity >= 1) {
        cout << "c [poly-shorten] shortened " << num_rewrites << " eqs"
             << " budget-left " << budget << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_rewrites;
}

///////////////////////////////////////////////////////////////////////////////
// lit-probe: forced literals, equivalences and implications from small
// equations, found by partial evaluation
//
// For an equation p = 0 in at most `config.probeVars` variables and a
// variable v, p|v=0 == 1 means v = 0 is impossible, so v = 1 (and the same
// for v = 1). For a pair (v, w) the four partial evaluations p|v=a,w=b == 1
// give the binary clauses implied by p; two of them together are an
// (anti-)equivalence v = w (+1), a single one is an implication. All
// implications are collected into a graph and its strongly connected
// components give further equivalences (impl-scc), exactly like SCC-based
// variable replacement in CNF preprocessors.
///////////////////////////////////////////////////////////////////////////////

namespace {

// Iterative Tarjan SCC on a graph over literals (2*nVars nodes).
void tarjan_scc(const vector<vector<uint32_t> >& adj, vector<uint32_t>& comp)
{
    const uint32_t n = adj.size();
    const uint32_t UNSEEN = std::numeric_limits<uint32_t>::max();
    vector<uint32_t> index(n, UNSEEN), low(n, 0);
    vector<char> on_stack(n, 0);
    vector<uint32_t> stack;
    comp.assign(n, UNSEEN);
    uint32_t next_index = 0, next_comp = 0;

    struct Frame {
        uint32_t node;
        size_t edge;
    };
    vector<Frame> call;
    for (uint32_t root = 0; root < n; root++) {
        if (index[root] != UNSEEN || adj[root].empty()) continue;
        call.push_back({root, 0});
        index[root] = low[root] = next_index++;
        stack.push_back(root);
        on_stack[root] = 1;
        while (!call.empty()) {
            Frame& fr = call.back();
            const uint32_t v = fr.node;
            if (fr.edge < adj[v].size()) {
                const uint32_t w = adj[v][fr.edge++];
                if (index[w] == UNSEEN) {
                    index[w] = low[w] = next_index++;
                    stack.push_back(w);
                    on_stack[w] = 1;
                    call.push_back({w, 0});
                } else if (on_stack[w]) {
                    low[v] = std::min(low[v], index[w]);
                }
                continue;
            }
            if (low[v] == index[v]) {
                while (true) {
                    const uint32_t w = stack.back();
                    stack.pop_back();
                    on_stack[w] = 0;
                    comp[w] = next_comp;
                    if (w == v) break;
                }
                next_comp++;
            }
            call.pop_back();
            if (!call.empty()) {
                const uint32_t u = call.back().node;
                low[u] = std::min(low[u], low[v]);
            }
        }
    }
}

inline uint32_t lit_node(uint32_t var, bool neg)
{
    return 2 * var + (neg ? 1 : 0);
}

}

size_t ANF::probe_small_polys()
{
    SimpStatsScope scope(*this, "lit-probe");
    const double myTime = cpuTime();
    const size_t max_vars = config.probeVars;
    size_t num_forced = 0, num_equiv = 0, num_impl = 0;

    vector<BoolePolynomial> facts;
    // implication graph over literals: node 2v = v is true, 2v+1 = v is false
    vector<vector<uint32_t> > adj(2 * ring->nVariables());
    auto add_clause2 = [&](uint32_t v, bool v_neg, uint32_t w, bool w_neg) {
        // clause (lv | lw): ~lv -> lw and ~lw -> lv
        adj[lit_node(v, !v_neg)].push_back(lit_node(w, w_neg));
        adj[lit_node(w, !w_neg)].push_back(lit_node(v, v_neg));
        num_impl++;
    };

    for (size_t i = 0; i < eqs.size(); i++) {
        if (!poly_valid[i] && nVarsOf(i) > max_vars) continue; // a long product
        const BoolePolynomial& p = eq(i);
        if (p.isConstant() || p.nUsedVariables() > max_vars) continue;
        if (p.nUsedVariables() <= 2 && p.deg() == 1) continue; // replacer's job

        vector<uint32_t> vars;
        for (const uint32_t v : p.usedVariables()) vars.push_back(v);
        const BooleSet pset = p.set();
        // p = p0 + v*p1, so p|v=0 = p0 and p|v=1 = p0 + p1
        auto eval_one = [&](const BooleSet& s, uint32_t v, bool val) -> BooleSet {
            const BooleSet s0 = s.subset0(v);
            if (!val) return s0;
            return s0.Xor(s.subset1(v));
        };
        auto is_one = [](const BooleSet& s) {
            return s.isSingleton() && s.ownsOne();
        };

        vector<char> forced(vars.size(), 0); // 1: forced to 0, 2: forced to 1
        for (size_t a = 0; a < vars.size(); a++) {
            const uint32_t v = vars[a];
            const BooleSet at0 = eval_one(pset, v, false);
            const BooleSet at1 = eval_one(pset, v, true);
            if (is_one(at0) && is_one(at1)) {
                // p can never be 0 -- UNSAT
                facts.push_back(BoolePolynomial(true, *ring));
                break;
            }
            if (is_one(at0)) {
                facts.push_back(BoolePolynomial(BooleVariable(v, *ring)) + BooleConstant(true));
                forced[a] = 2;
                num_forced++;
            } else if (is_one(at1)) {
                facts.push_back(BoolePolynomial(BooleVariable(v, *ring)));
                forced[a] = 1;
                num_forced++;
            }
        }

        for (size_t a = 0; a < vars.size(); a++) {
            if (forced[a]) continue;
            const uint32_t v = vars[a];
            const BooleSet v0 = eval_one(pset, v, false);
            const BooleSet v1 = eval_one(pset, v, true);
            for (size_t b = a + 1; b < vars.size(); b++) {
                if (forced[b]) continue;
                const uint32_t w = vars[b];
                // bit (2*va + vb) set: assignment v=va, w=vb is impossible
                unsigned bad = 0;
                if (is_one(eval_one(v0, w, false))) bad |= 1 << 0;
                if (is_one(eval_one(v0, w, true)))  bad |= 1 << 1;
                if (is_one(eval_one(v1, w, false))) bad |= 1 << 2;
                if (is_one(eval_one(v1, w, true)))  bad |= 1 << 3;
                if (bad == 0) continue;
                if ((bad & 0x9) == 0x9) { // (0,0) and (1,1) impossible: v = w + 1
                    facts.push_back(BoolePolynomial(BooleVariable(v, *ring))
                                    + BooleVariable(w, *ring) + BooleConstant(true));
                    num_equiv++;
                    continue;
                }
                if ((bad & 0x6) == 0x6) { // (0,1) and (1,0) impossible: v = w
                    facts.push_back(BoolePolynomial(BooleVariable(v, *ring))
                                    + BooleVariable(w, *ring));
                    num_equiv++;
                    continue;
                }
                // each impossible assignment (va, vb) is the clause (v != va | w != vb)
                for (unsigned k = 0; k < 4; k++) {
                    if (!(bad & (1u << k))) continue;
                    const bool va = k & 2, vb = k & 1;
                    add_clause2(v, va, w, vb);
                }
            }
        }
    }

    // SCCs of the implication graph: all literals in one component are equal.
    // Components come in mirrored pairs (negate every literal); the
    // representative of a component is its smallest node, so exactly one of
    // the two mirrors has a positive-literal (even) representative, and only
    // that one is used to avoid reporting every equivalence twice.
    size_t num_scc_equiv = 0;
    {
        vector<uint32_t> comp;
        tarjan_scc(adj, comp);
        vector<uint32_t> rep(comp.size(), std::numeric_limits<uint32_t>::max());
        for (uint32_t node = 0; node < comp.size(); node++) {
            const uint32_t c = comp[node];
            if (c == std::numeric_limits<uint32_t>::max()) continue;
            if (rep[c] == std::numeric_limits<uint32_t>::max()) {
                rep[c] = node;
                continue;
            }
            const uint32_t r = rep[c];
            if (r & 1) continue; // handled through the mirror component
            const uint32_t v = r / 2, w = node / 2;
            if (v == w) {
                // v and ~v in the same component: UNSAT
                facts.push_back(BoolePolynomial(true, *ring));
                continue;
            }
            const bool inv = ((r ^ node) & 1);
            facts.push_back(BoolePolynomial(BooleVariable(v, *ring))
                            + BooleVariable(w, *ring) + BooleConstant(inv));
            num_scc_equiv++;
        }
    }

    size_t num_added = 0;
    for (const BoolePolynomial& f : facts) {
        num_added += addBoolePolynomial(f);
    }
    if (num_added > 0) {
        if (!propagate()) setNOTOK();
    }

    if (config.verbosity >= 1) {
        cout << "c [lit-probe] forced " << num_forced << " equiv " << num_equiv
             << " impl " << num_impl << " scc-equiv " << num_scc_equiv
             << " new-facts " << num_added << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_added;
}

///////////////////////////////////////////////////////////////////////////////
// fac-canon: canonical linear factors
//
// The linear equations of the system span a space L. Two linear factors that
// differ by an element of L are the same constraint on every solution, so
// every factor is reduced to its normal form modulo L (Gaussian elimination
// with the highest variable of each equation as pivot) and then replaced by
// the shortest factor of its class (ties: lexicographically first), with the
// constant adjusted. Factors that are constant modulo L drop out: a factor
// equal to 0 makes the equation trivially true, one equal to 1 is removed.
// Sound because the linear equations stay in the system: adding an equation
// to a factor of a product does not change the solutions of the system.
///////////////////////////////////////////////////////////////////////////////

size_t ANF::canon_factors()
{
    SimpStatsScope scope(*this, "fac-canon");
    const double myTime = cpuTime();
    const size_t n = ring->nVariables();

    // --- Gaussian elimination of the linear equations, pivot = highest var.
    // A row is a bit vector over the variables plus a constant bit.
    typedef vector<uint64_t> Row;
    const size_t words = (n + 63) / 64;
    auto bit = [&](const Row& r, size_t i) { return (r[i / 64] >> (i % 64)) & 1; };
    auto flip = [&](Row& r, size_t i) { r[i / 64] ^= (uint64_t)1 << (i % 64); };
    auto highest = [&](const Row& r) -> long {
        for (long w = words - 1; w >= 0; w--) if (r[w]) return w * 64 + (63 - __builtin_clzll(r[w]));
        return -1;
    };
    std::unordered_map<uint32_t, Row> rows; // pivot -> row (words bits + [words] constant)
    auto reduce = [&](Row& r) {
        // eliminate pivots from the top down
        while (true) {
            bool did = false;
            for (long w = words - 1; w >= 0 && !did; w--) {
                uint64_t x = r[w];
                while (x) {
                    const size_t i = w * 64 + (63 - __builtin_clzll(x));
                    x &= ~((uint64_t)1 << (i % 64));
                    auto it = rows.find(i);
                    if (it == rows.end()) continue;
                    for (size_t k = 0; k <= words; k++) r[k] ^= it->second[k];
                    did = true;
                    break;
                }
            }
            if (!did) break;
        }
    };
    size_t num_lin = 0;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (isProduct(i) || degOf(i) != 1) continue;
        const BoolePolynomial& p = eqs[i];
        Row r(words + 1, 0);
        for (const uint32_t v : p.usedVariables()) flip(r, v);
        r[words] = p.hasConstantPart();
        reduce(r);
        const long piv = highest(r);
        if (piv < 0) continue; // 0 = 0 (or 1 = 0, which propagation reports)
        // keep the other rows reduced too (Gauss-Jordan)
        for (auto& kv : rows) {
            if (bit(kv.second, piv)) for (size_t k = 0; k <= words; k++) kv.second[k] ^= r[k];
        }
        rows[piv] = r;
        num_lin++;
    }

    // --- normal form of every factor of every product, and the shortest
    // representative of each class
    struct Rep { Lineral l; bool d; }; // XOR(l.vars) = XOR(nf) + d
    std::unordered_map<VarVec, Rep, VarVecHash> rep;
    auto normal_form = [&](const Lineral& l, VarVec& nf, bool& d) {
        Row r(words + 1, 0);
        for (const uint32_t v : l.vars) flip(r, v);
        reduce(r);
        nf.clear();
        for (size_t w = 0; w < words; w++) {
            uint64_t x = r[w];
            while (x) {
                const size_t i = w * 64 + __builtin_ctzll(x);
                x &= x - 1;
                nf.push_back(i);
            }
        }
        d = r[words]; // XOR(l) = XOR(nf) + d modulo the span
    };
    auto better = [](const Lineral& a, const Lineral& b) { // a preferred over b
        if (a.vars.size() != b.vars.size()) return a.vars.size() < b.vars.size();
        return a.vars < b.vars;
    };
    VarVec nf;
    bool d;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (!isProduct(i)) continue;
        for (const Lineral& l : factors[i]) {
            normal_form(l, nf, d);
            if (nf.empty()) continue;
            Lineral cand;
            cand.vars = l.vars;
            cand.c = false;
            // the normal form itself is a candidate representative too
            Lineral nfl;
            nfl.vars = nf;
            auto it = rep.find(nf);
            if (it == rep.end()) {
                Rep r0 = {cand, d};
                if (better(nfl, cand)) { r0.l = nfl; r0.d = false; }
                rep[nf] = r0;
            } else {
                if (better(cand, it->second.l)) it->second = Rep{cand, d};
                if (better(nfl, it->second.l)) it->second = Rep{nfl, false};
            }
        }
    }

    // --- rewrite the factors
    unordered_set<uint32_t> updatedVars;
    vector<size_t> empty_equations;
    size_t num_changed = 0, num_const = 0, num_replaced = 0;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (!isProduct(i)) continue;
        vector<Lineral> nfac;
        bool changed = false, zero = false;
        for (const Lineral& l : factors[i]) {
            normal_form(l, nf, d);
            if (nf.empty()) {
                // the factor is the constant (d + c) on every solution
                num_const++;
                changed = true;
                if ((d ^ l.c) == 0) { zero = true; break; } // factor 0: equation is 0 = 0
                continue; // factor 1: drop it
            }
            const Rep& r = rep[nf];
            Lineral nl;
            nl.vars = r.l.vars;
            nl.c = l.c ^ d ^ r.d; // XOR(l) = XOR(rep) + d + r.d
            if (!(nl == l)) { changed = true; num_replaced++; }
            nfac.push_back(nl);
        }
        if (!changed) continue;
        num_changed++;
        if (config.verbosity >= 5) {
            cout << "c [fac-canon] eq " << i << " factors " << factors[i].size()
                 << " -> " << (zero ? 0 : nfac.size()) << endl;
        }
        if (zero) {
            if (!updateEquations(i, BoolePolynomial(*ring), empty_equations, nullptr)) return num_changed;
            continue;
        }
        if (nfac.empty()) { // product of nothing = 1: UNSAT
            replacer->setNOTOK();
            return num_changed;
        }
        if (nfac.size() == 1) {
            const BoolePolynomial lin = expand_linerals(*ring, nfac);
            if (!updateEquations(i, lin, empty_equations, nullptr)) return num_changed;
            if (poly_valid[i] && !eqs[i].isConstant()) check_if_need_update(eqs[i], updatedVars);
            continue;
        }
        if (!updateEquations(i, BoolePolynomial(*ring), empty_equations, &nfac)) return num_changed;
    }
    finish_rewrites(updatedVars, empty_equations);

    if (config.verbosity >= 1) {
        cout << "c [fac-canon] linear rank " << num_lin << " classes " << rep.size()
             << " eqs changed " << num_changed << " factors replaced " << num_replaced
             << " constant factors " << num_const << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_changed;
}

///////////////////////////////////////////////////////////////////////////////
// fac-res: resolution on linear factors
//
// (A + a) * R1 = 0 and (A + a + 1) * R2 = 0 give R1 * R2 = 0: if A = a the
// second forces R2 = 0, otherwise the first forces R1 = 0. Resolvents with a
// single factor are new linear equations (e.g. (A)(B) = 0 and (A+1)(B) = 0
// give B = 0); resolvents with two factors are new short products. Longer
// ones are not added (config.facResMaxFactors).
///////////////////////////////////////////////////////////////////////////////

size_t ANF::resolve_factors()
{
    SimpStatsScope scope(*this, "fac-res");
    const double myTime = cpuTime();

    // factor variables -> products containing it, per constant
    std::unordered_map<VarVec, std::pair<vector<size_t>, vector<size_t> >, VarVecHash> occ;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (!isProduct(i)) continue;
        for (const Lineral& l : factors[i]) {
            auto& e = occ[l.vars];
            (l.c ? e.second : e.first).push_back(i);
        }
    }

    vector<vector<Lineral> > new_products;
    vector<BoolePolynomial> new_linear;
    std::unordered_set<VarVec, VarVecHash> seen;
    size_t pairs = 0;
    const size_t max_pairs = 2000000;
    for (const auto& kv : occ) {
        const vector<size_t>& with0 = kv.second.first;
        const vector<size_t>& with1 = kv.second.second;
        if (with0.empty() || with1.empty()) continue;
        for (const size_t p : with0) {
            for (const size_t q : with1) {
                if (++pairs > max_pairs) break;
                // resolvent: all factors of p and q except the resolved one
                vector<Lineral> res;
                bool taut = false;
                for (const size_t idx : {p, q}) {
                    for (const Lineral& l : factors[idx]) {
                        if (l.vars == kv.first) continue;
                        bool dup = false;
                        for (const Lineral& m : res) {
                            if (m.vars == l.vars) {
                                if (m.c != l.c) taut = true;
                                dup = true;
                            }
                        }
                        if (!dup) res.push_back(l);
                    }
                }
                if (taut || res.empty()) continue;
                if (res.size() > config.facResMaxFactors) continue;
                if (res.size() == 1) {
                    new_linear.push_back(expand_linerals(*ring, res));
                } else {
                    const VarVec key = product_key(res);
                    if (prod_keys.count(key) || !seen.insert(key).second) continue;
                    new_products.push_back(res);
                }
            }
        }
    }

    size_t added_lin = 0, added_prod = 0;
    for (const BoolePolynomial& l : new_linear) added_lin += addBoolePolynomial(l);
    for (const vector<Lineral>& f : new_products) added_prod += addProduct(f);
    if (added_lin > 0) {
        if (!propagate()) setNOTOK();
    }

    if (config.verbosity >= 1) {
        cout << "c [fac-res] pairs " << pairs << " new linear " << added_lin
             << " new products " << added_prod << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return added_lin + added_prod;
}

///////////////////////////////////////////////////////////////////////////////
// Driver: run the in-place rules until nothing changes
///////////////////////////////////////////////////////////////////////////////

size_t ANF::rewrite_inplace()
{
    size_t total = 0;
    if (keep_factor == -1) {
        if (config.keepFactor != 2) {
            keep_factor = config.keepFactor;
        } else {
            // auto: product-preserving when most nonlinear equations are
            // products of linear factors (e.g. XNF written out as polynomials)
            size_t nonlin = 0, products = 0;
            vector<Lineral> f;
            for (size_t i = 0; i < eqs.size(); i++) {
                if (degOf(i) < 2) continue;
                nonlin++;
                if (isProduct(i) || (factor_into_linerals(eq(i), f) && f.size() >= 2)) products++;
            }
            keep_factor = (nonlin > 0 && 2 * products >= nonlin) ? 1 : 0;
            if (config.verbosity >= 1) {
                cout << "c [rewrite] " << products << "/" << nonlin
                     << " nonlinear eqs are products of linerals, product-preserving mode: "
                     << keep_factor << endl;
            }
        }
    }
    for (unsigned round = 0; round < config.rewriteRounds; round++) {
        if (!getOK()) break;
        size_t changes = 0;
        if (config.doBinomRed) changes += reduce_by_short_polys();
        if (!getOK()) break;
        if (config.doShorten) changes += shorten_polys();
        if (!getOK()) break;
        if (config.doProbe) changes += probe_small_polys();
        if (!getOK()) break;
        if (config.doFacCanon) changes += canon_factors();
        if (!getOK()) break;
        if (config.doFacRes) changes += resolve_factors();
        total += changes;
        if (changes == 0) break;
        if (cpuTime() > config.maxTime) break;
    }
    return total;
}
