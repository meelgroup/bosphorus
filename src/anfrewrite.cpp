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
#include "linbasis.hpp"
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
// var-probe: failed-literal probing with propagation
//
// The probing of CNF preprocessors, lifted to the ANF: x = 0 is assumed
// and propagated (an equation all of whose other variables are assigned
// becomes a constant or a unit; a product with all factors but one equal
// to 1 forces the last factor to 0; m + 1 = 0 sets every variable of m),
// then x = 1. A branch that reaches 1 = 0 forces x to the other value; a
// variable that both branches set to the same value is set; one that they
// set to opposite values equals x or x + 1. lit-probe looks at one
// equation at a time; this rule follows chains through the system.
///////////////////////////////////////////////////////////////////////////////

bool ANF::propagate_assumption(uint32_t v0, bool val0, vector<lbool>& assign,
                               vector<uint32_t>& trail, int64_t& budget) const
{
    auto set = [&](uint32_t v, bool val) -> bool {
        if (assign[v] != l_Undef) return assign[v] == (val ? l_True : l_False);
        assign[v] = val ? l_True : l_False;
        trail.push_back(v);
        return true;
    };
    if (!set(v0, val0)) return true;
    size_t head = trail.size() - 1;
    while (head < trail.size()) {
        const uint32_t u = trail[head++];
        for (const size_t i : occur[u]) {
            if (budget <= 0) return true; // out of budget: whatever was found so far holds
            budget--;
            if (!poly_valid[i]) {
                // a product: factors that are constant under the assignment
                bool zero = false;
                size_t open = 0;      // factors not yet constant
                const Lineral* last = nullptr;
                uint32_t last_free = 0;
                size_t last_nfree = 0;
                bool last_c = false;
                for (const Lineral& l : factors[i]) {
                    bool c = l.c;
                    size_t nfree = 0;
                    uint32_t free_var = 0;
                    for (const uint32_t w : l.vars) {
                        if (assign[w] == l_Undef) { nfree++; free_var = w; }
                        else if (assign[w] == l_True) c = !c;
                    }
                    if (nfree == 0) {
                        if (!c) { zero = true; break; } // factor 0: equation satisfied
                        continue;                       // factor 1: drops out
                    }
                    open++;
                    last = &l; last_free = free_var; last_nfree = nfree; last_c = c;
                }
                if (zero) continue;
                if (open == 0) return false;            // every factor is 1: 1 = 0
                if (open == 1 && last_nfree == 1) {
                    // the last factor must be 0: free_var + c = 0
                    if (!set(last_free, last_c)) return false;
                }
                (void)last;
                continue;
            }
            if (eq_len[i] > config.varProbeLen) continue;
            const BoolePolynomial& p = eqs[i];
            BooleSet s = p.set();
            bool any = false;
            for (const uint32_t w : p.usedVariables()) {
                if (assign[w] == l_Undef) continue;
                any = true;
                const BooleSet s0 = s.subset0(w);
                s = (assign[w] == l_True) ? s0.Xor(s.subset1(w)) : s0;
            }
            if (!any) continue;
            if (s.isZero()) continue;
            if (s.isSingleton() && s.ownsOne()) return false; // 1 = 0
            const BoolePolynomial q(s);
            if (q.nUsedVariables() == 1 && q.deg() == 1) {
                // y or y + 1
                const uint32_t y = q.usedVariables().firstVariable().index();
                if (!set(y, q.hasConstantPart())) return false;
                continue;
            }
            if (q.isPair() && q.hasConstantPart()) {
                // m + 1 = 0: every variable of m is 1
                for (const uint32_t y : q.firstTerm()) {
                    if (!set(y, true)) return false;
                }
            }
        }
    }
    return true;
}

size_t ANF::probe_vars()
{
    SimpStatsScope scope(*this, "var-probe");
    const double myTime = cpuTime();
    int64_t budget = config.varProbeBudget;
    const size_t n = ring->nVariables();

    // candidates: active variables, the ones in the most equations first
    vector<uint32_t> cand;
    for (uint32_t v = 0; v < n; v++) {
        if (occur[v].empty()) continue;
        if (replacer->getValue(v) != l_Undef || replacer->getReplaced(v) != Lit(v, false)) continue;
        cand.push_back(v);
    }
    std::stable_sort(cand.begin(), cand.end(), [&](uint32_t a, uint32_t b) {
        return occur[a].size() > occur[b].size();
    });

    vector<lbool> a0(n, l_Undef), a1(n, l_Undef);
    vector<uint32_t> t0, t1;
    vector<BoolePolynomial> facts;
    size_t probes = 0, failed = 0, units = 0, equivs = 0;
    bool unsat = false;
    for (const uint32_t v : cand) {
        if (budget <= 0 || unsat) break;
        probes++;
        for (const uint32_t w : t0) a0[w] = l_Undef;
        for (const uint32_t w : t1) a1[w] = l_Undef;
        t0.clear();
        t1.clear();
        const bool ok0 = propagate_assumption(v, false, a0, t0, budget);
        const bool ok1 = propagate_assumption(v, true, a1, t1, budget);
        if (!ok0 && !ok1) { unsat = true; facts.push_back(BoolePolynomial(true, *ring)); break; }
        if (!ok0 || !ok1) {
            // one branch is impossible: v is forced, and everything the
            // other branch derived holds
            failed++;
            const vector<uint32_t>& t = ok0 ? t0 : t1;
            const vector<lbool>& a = ok0 ? a0 : a1;
            for (const uint32_t w : t) {
                facts.push_back(BoolePolynomial(BooleVariable(w, *ring)) + BooleConstant(a[w] == l_True));
                units++;
            }
            continue;
        }
        // both possible: what they agree on, and what they decide oppositely
        for (const uint32_t w : t0) {
            if (w == v || a1[w] == l_Undef) continue;
            if (a0[w] == a1[w]) {
                facts.push_back(BoolePolynomial(BooleVariable(w, *ring)) + BooleConstant(a0[w] == l_True));
                units++;
            } else {
                // w = v + c: w is a0[w] when v = 0
                facts.push_back(BoolePolynomial(BooleVariable(w, *ring)) + BooleVariable(v, *ring)
                                + BooleConstant(a0[w] == l_True));
                equivs++;
            }
        }
    }

    size_t num_added = 0;
    for (const BoolePolynomial& f : facts) num_added += addBoolePolynomial(f);
    if (num_added > 0) {
        if (!propagate()) setNOTOK();
    }
    if (config.verbosity >= 1) {
        cout << "c [var-probe] probed " << probes << "/" << cand.size() << " vars, failed " << failed
             << " units " << units << " equivs " << equivs << " new-facts " << num_added
             << " budget-left " << budget << " T: " << std::fixed << std::setprecision(2)
             << (cpuTime() - myTime) << endl;
    }
    return num_added;
}

///////////////////////////////////////////////////////////////////////////////
// lin-gauss: Gaussian elimination among the linear equations
//
// The linear equations are inserted, shortest first, into an echelon basis
// (pivot = highest variable, kept fully reduced). An equation that reduces
// to 0 is a combination of shorter ones and is deleted; one whose reduced
// form has fewer terms is replaced by it (sound: the equations it was
// reduced by stay in the system, and the span of the linear part never
// changes). Reduced forms with one or two variables are units and
// equivalences that propagation then substitutes everywhere. Nonlinear
// equations are never touched, so nothing can grow.
///////////////////////////////////////////////////////////////////////////////

size_t ANF::gauss_linear()
{
    SimpStatsScope scope(*this, "lin-gauss");
    const double myTime = cpuTime();
    LinBasis basis(ring->nVariables());

    // shortest first, ties by index: the short equations become the basis
    // and the long ones are reduced by them
    vector<size_t> order;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (isProduct(i) || degOf(i) != 1) continue;
        order.push_back(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return eq_len[a] < eq_len[b];
    });

    unordered_set<uint32_t> updatedVars;
    vector<size_t> empty_equations;
    size_t num_deleted = 0, num_shortened = 0;
    const size_t num_lin = order.size();
    for (const size_t i : order) {
        LinBasis::Row r = basis.row_of(eqs[i]);
        const size_t len_before = basis.length(r);
        basis.reduce(r);
        if (basis.pivot(r) < 0) {
            if (basis.constant(r)) {
                // reduced to 1 = 0: UNSAT
                replacer->setNOTOK();
                return num_deleted + num_shortened + 1;
            }
            // a combination of the equations already in the basis
            num_deleted++;
            if (!rewrite_eq(i, BoolePolynomial(*ring), updatedVars, empty_equations)) {
                return num_deleted + num_shortened;
            }
            continue;
        }
        if (basis.length(r) < len_before) {
            const BoolePolynomial np = basis.poly_of(r, *ring);
            if (config.verbosity >= 5) {
                cout << "c [lin-gauss] " << eqs[i] << "  -->  " << np << endl;
            }
            num_shortened++;
            if (!rewrite_eq(i, np, updatedVars, empty_equations)) {
                return num_deleted + num_shortened;
            }
        }
        basis.insert(r);
    }
    finish_rewrites(updatedVars, empty_equations);

    if (config.verbosity >= 1) {
        cout << "c [lin-gauss] linear " << num_lin << " rank " << basis.rank()
             << " deleted " << num_deleted << " shortened " << num_shortened
             << " T: " << std::fixed << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_deleted + num_shortened;
}

///////////////////////////////////////////////////////////////////////////////
// mono-gauss: Gaussian elimination over the monomials
//
// Every monomial is a column (linearisation, the degree-0 step of XL) and
// the equations, shortest first, are inserted into an echelon basis whose
// pivot is the deglex-largest monomial of a row. An equation that reduces
// to 0 is a combination of shorter ones and is deleted; one whose reduced
// form is shorter, or of lower degree (all nonlinear monomials cancelled:
// a linear consequence), is replaced by it. Sound like lin-gauss: the rows
// it was reduced by stay in the system. Reduction by a row only brings in
// monomials below its pivot, so no degree ever grows.
///////////////////////////////////////////////////////////////////////////////

size_t ANF::gauss_monomials()
{
    SimpStatsScope scope(*this, "mono-gauss");
    const double myTime = cpuTime();
    typedef BooleMonomial::hash_type mhash;

    vector<size_t> order;
    for (size_t i = 0; i < eqs.size(); i++) {
        if (!poly_valid[i] || eq_len[i] == 0 || eq_len[i] > config.monoGaussLen) continue;
        if (keep_factor == 1 && degOf(i) >= 2) continue;
        if (eqs[i].isConstant() || degOf(i) < 1) continue;
        order.push_back(i);
    }
    if (order.size() < 2) return 0;

    // the columns: all monomials, deglex ascending so that the highest
    // column (the pivot) is the leading monomial
    unordered_map<mhash, uint32_t> col_of;
    vector<BooleMonomial> mons;
    for (const size_t i : order) {
        for (const BooleMonomial& t : eqs[i]) {
            if (t.deg() == 0) continue;
            if (col_of.emplace(t.stableHash(), 0).second) mons.push_back(t);
            if (mons.size() > config.monoGaussCols) break;
        }
        if (mons.size() > config.monoGaussCols) break;
    }
    if (mons.size() > config.monoGaussCols) {
        if (config.verbosity >= 1) {
            cout << "c [mono-gauss] more than " << config.monoGaussCols << " distinct monomials: skipped" << endl;
        }
        return 0;
    }
    std::sort(mons.begin(), mons.end(), deglex_less);
    for (uint32_t c = 0; c < mons.size(); c++) col_of[mons[c].stableHash()] = c;

    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return eq_len[a] < eq_len[b];
    });

    LinBasis basis(mons.size());
    unordered_set<uint32_t> updatedVars;
    vector<size_t> empty_equations;
    size_t num_deleted = 0, num_shortened = 0, num_degfall = 0;
    for (const size_t i : order) {
        LinBasis::Row r = basis.blank();
        for (const BooleMonomial& t : eqs[i]) {
            if (t.deg() == 0) basis.set_constant(r, true);
            else LinBasis::set_col(r, col_of[t.stableHash()]);
        }
        const size_t len_before = basis.length(r) + basis.constant(r);
        const int deg_before = eqs[i].deg();
        basis.reduce(r);
        const long piv = basis.pivot(r);
        if (piv < 0) {
            if (basis.constant(r)) {
                replacer->setNOTOK(); // reduced to 1 = 0
                return num_deleted + num_shortened + 1;
            }
            num_deleted++;
            if (!rewrite_eq(i, BoolePolynomial(*ring), updatedVars, empty_equations)) {
                return num_deleted + num_shortened;
            }
            continue;
        }
        const size_t len_after = basis.length(r) + basis.constant(r);
        const int deg_after = mons[piv].deg();
        const bool may_shorten = config.monoGaussShorten == 2 || (config.monoGaussShorten == 1 && deg_before <= 1);
        if ((may_shorten && len_after < len_before) || deg_after < deg_before) {
            // the polynomial of the row, summed pairwise
            vector<BoolePolynomial> level;
            basis.for_each_col(r, [&](size_t c) { level.push_back(BoolePolynomial(mons[c])); });
            while (level.size() > 1) {
                vector<BoolePolynomial> next;
                for (size_t k = 0; k + 1 < level.size(); k += 2) next.push_back(level[k] + level[k + 1]);
                if (level.size() % 2) next.push_back(level.back());
                level.swap(next);
            }
            BoolePolynomial np(basis.constant(r), *ring);
            if (!level.empty()) np += level.front();
            if (config.verbosity >= 5) {
                cout << "c [mono-gauss] " << eqs[i] << "  -->  " << np << endl;
            }
            if (deg_after < deg_before) num_degfall++;
            else num_shortened++;
            if (!rewrite_eq(i, np, updatedVars, empty_equations)) {
                return num_deleted + num_shortened + num_degfall;
            }
        }
        basis.insert(r);
    }
    finish_rewrites(updatedVars, empty_equations);

    if (config.verbosity >= 1) {
        cout << "c [mono-gauss] eqs " << order.size() << " monomials " << mons.size()
             << " rank " << basis.rank() << " deleted " << num_deleted
             << " shortened " << num_shortened << " degree-falls " << num_degfall
             << " T: " << std::fixed << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_deleted + num_shortened + num_degfall;
}

///////////////////////////////////////////////////////////////////////////////
// prod-split: an equation whose complement is a product of linerals
//
// p = 0 means 1 + p = 1. If 1 + p = (l1 + c1) * ... * (lk + ck) then every
// factor must be 1, so p = 0 is equivalent to the k linear equations
// li + ci + 1 = 0. The rule "x*y*z + 1 = 0 sets x, y, z" of anf-prop is the
// case of single-variable factors; x*y + x + y = 0 (x or y) gives x = 0
// and y = 0. lit-probe finds these for equations in a few variables by
// evaluation; here the factors may be long.
///////////////////////////////////////////////////////////////////////////////

size_t ANF::split_products()
{
    SimpStatsScope scope(*this, "prod-split");
    const double myTime = cpuTime();
    size_t num_split = 0, num_linear = 0;
    unordered_set<uint32_t> updatedVars;
    vector<size_t> empty_equations;
    vector<BoolePolynomial> extra;
    const size_t n_eqs = eqs.size();
    for (size_t i = 0; i < n_eqs; i++) {
        if (!poly_valid[i] || eq_len[i] < 3 || degOf(i) < 2) continue;
        const BoolePolynomial& p = eqs[i];
        // 1 + p is a product of >= 2 non-constant factors only if it has no
        // constant term (every factor has a variable) -- p has one -- and
        // at least 2^2 - ... terms: a factor of one variable and a factor
        // of one variable give x*y (one term); the check is cheap anyway
        if (!p.hasConstantPart()) continue;
        const BoolePolynomial q = p + BooleConstant(true);
        vector<Lineral> f;
        if (!factor_into_linerals(q, f) || f.size() < 2) continue;
        vector<BoolePolynomial> lin;
        for (const Lineral& l : f) {
            vector<Lineral> one(1, l);
            lin.push_back(expand_linerals(*ring, one) + BooleConstant(true)); // l + c + 1 = 0
        }
        if (config.verbosity >= 5) {
            cout << "c [prod-split] " << p << "  -->  " << f.size() << " linear equations" << endl;
        }
        num_split++;
        num_linear += lin.size();
        if (!rewrite_eq(i, lin[0], updatedVars, empty_equations)) return num_split;
        for (size_t k = 1; k < lin.size(); k++) extra.push_back(lin[k]);
    }
    for (const BoolePolynomial& l : extra) {
        if (addBoolePolynomial(l)) check_if_need_update(l, updatedVars);
    }
    finish_rewrites(updatedVars, empty_equations);
    if (config.verbosity >= 1 && num_split > 0) {
        cout << "c [prod-split] split " << num_split << " eqs into " << num_linear
             << " linear ones T: " << std::fixed << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return num_split;
}

///////////////////////////////////////////////////////////////////////////////
// Facts learnt by a strategy (XL, ElimLin, SAT) are often linear
// combinations of equations the system already has: they add nothing but
// long XORs to the CNF. Linear facts are therefore reduced modulo the span
// of the current linear equations; the ones in the span are dropped, the
// others are added in the shorter of their two forms.
///////////////////////////////////////////////////////////////////////////////

size_t ANF::add_linearly_new_facts(const vector<BoolePolynomial>& facts, bool contextualize)
{
    LinBasis basis(ring->nVariables());
    for (size_t i = 0; i < eqs.size(); i++) {
        if (isProduct(i) || degOf(i) != 1) continue;
        LinBasis::Row r = basis.row_of(eqs[i]);
        basis.reduce(r);
        if (basis.pivot(r) >= 0) basis.insert(r);
    }

    size_t num_added = 0, num_in_span = 0;
    for (const BoolePolynomial& f : facts) {
        const BoolePolynomial p = contextualize ? replacer->update(f) : f;
        if (p.deg() > 1 || p.isZero()) {
            num_added += addBoolePolynomial(p);
            continue;
        }
        LinBasis::Row r = basis.row_of(p);
        const size_t len_before = basis.length(r);
        basis.reduce(r);
        if (basis.pivot(r) < 0) {
            if (basis.constant(r)) {
                num_added += addBoolePolynomial(BoolePolynomial(true, *ring)); // UNSAT
                continue;
            }
            num_in_span++;
            // a unit or an equivalence is absorbed by the replacer (no XOR
            // is left behind) and propagates: always worth adding, even if
            // Gaussian elimination could in principle derive it
            if (!config.spanFilter || p.nUsedVariables() <= 2) num_added += addBoolePolynomial(p);
            continue;
        }
        basis.insert(r);
        num_added += addBoolePolynomial(basis.length(r) < len_before ? basis.poly_of(r, *ring) : p);
    }
    if (config.verbosity >= 2 && num_in_span > 0) {
        cout << "c [learnt] " << num_in_span << " linear fact(s) already in the span of the linear equations, "
             << num_added << " added" << endl;
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
        // The rows are pairwise reduced (each pivot is the highest variable
        // of its row and occurs in no other row), so one pass from the
        // highest bit down eliminates every pivot: adding a pivot row only
        // changes bits below its pivot.
        for (long w = words - 1; w >= 0; w--) {
            uint64_t mask = ~(uint64_t)0; // bits of r[w] still to look at
            while (r[w] & mask) {
                const size_t b = 63 - __builtin_clzll(r[w] & mask);
                mask &= ((uint64_t)1 << b) - 1;
                auto it = rows.find(w * 64 + b);
                if (it == rows.end()) continue;
                for (size_t k = 0; k <= words; k++) r[k] ^= it->second[k];
            }
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
                if (p == q) continue; // a product with l and l+1 is 0: no equation at all
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
        if (config.doLinGauss) changes += gauss_linear();
        if (!getOK()) break;
        if (config.doBinomRed) changes += reduce_by_short_polys();
        if (!getOK()) break;
        if (config.doProdSplit) changes += split_products();
        if (!getOK()) break;
        if (config.doShorten) changes += shorten_polys();
        if (!getOK()) break;
        if (config.doMonoGauss) changes += gauss_monomials();
        if (!getOK()) break;
        if (config.doProbe) changes += probe_small_polys();
        if (!getOK()) break;
        if (config.doVarProbe) changes += probe_vars();
        if (!getOK()) break;
        if (config.doFacCanon) changes += canon_factors();
        if (!getOK()) break;
        if (config.doFacRes) changes += resolve_factors();
        if (!getOK()) break;
        // cnf-probe (CryptoMiniSat's inprocessing on the CNF of the
        // system) costs a CNF conversion, so it runs once per round after
        // the cheap rules and before the Groebner engine, and only on a
        // system that changed since its last run (a unit or equivalence
        // it finds shrinks the basis computation)
        if (config.doCnfProbe && getOK()) {
            const BLib::ANFStats now = get_stats();
            const bool same = cp_ran && now.eqs == cp_last.eqs && now.monoms == cp_last.monoms &&
                              now.set_vars == cp_last.set_vars && now.repl_vars == cp_last.repl_vars &&
                              now.lin_eqs == cp_last.lin_eqs;
            if (!same) {
                changes += cnf_probe();
                cp_last = get_stats();
                cp_ran = true;
            }
        }
        if (!getOK()) break;
        // gb-cone: on request, or automatically on a small system (few
        // free variables), whose complete Groebner basis is cheap and often
        // solves it outright (random MQ systems up to ~28 variables)
        const bool gb_auto = config.doGB == 2 && config.gbFull && config.gbWholeVars > 0 &&
                             numActiveVars() <= config.gbWholeVars;
        if (config.doGB == 1 || gb_auto) {
            // expensive: only when the system changed since the last run
            // (same equations, same replacer state); new facts change the
            // cones of the next round, so the rule runs to a fixed point
            const BLib::ANFStats now = get_stats();
            const bool same = gb_ran && now.eqs == gb_last.eqs && now.monoms == gb_last.monoms &&
                              now.set_vars == gb_last.set_vars && now.repl_vars == gb_last.repl_vars &&
                              now.lin_eqs == gb_last.lin_eqs;
            if (!same) {
                changes += groebner_windows();
                gb_last = get_stats();
                gb_ran = true;
            }
        }
        total += changes;
        if (changes == 0) break;
        if (cpuTime() > config.maxTime) break;
    }
    return total;
}
