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

#include "linfactor.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <cassert>
#include <unordered_map>
#include <unordered_set>
#include <limits>

USING_NAMESPACE_PBORI
using std::vector;
using namespace BLib;

// In a product of linear factors with disjoint variables, two variables
// occur together in some monomial exactly when they are in different
// factors (a monomial takes at most one variable from each factor, and any
// two variables from different factors do appear together). So the factors
// are the classes of variables with identical co-occurrence sets. The
// constant of a factor is read off a monomial made of one variable from
// every other factor, and the guess is verified by re-expanding it.
bool BLib::factor_into_linerals(const BoolePolynomial& poly,
                                vector<Lineral>& factors)
{
    factors.clear();
    if (poly.isConstant()) return false;
    const BoolePolyRing& ring = poly.ring();

    vector<uint32_t> vars;
    for (const uint32_t v : poly.usedVariables()) vars.push_back(v);
    const size_t n = vars.size();
    std::unordered_map<uint32_t, uint32_t> idx;
    for (size_t i = 0; i < n; i++) idx[vars[i]] = i;

    if (poly.deg() == 1) {
        Lineral l;
        l.vars = vars;
        l.c = poly.hasConstantPart();
        factors.push_back(l);
        return true;
    }

    // co-occurrence matrix
    vector<vector<char> > adj(n, vector<char>(n, 0));
    vector<uint32_t> mvars;
    for (const BooleMonomial& m : poly) {
        if (m.deg() < 2) continue;
        mvars.clear();
        for (const uint32_t v : m) mvars.push_back(idx[v]);
        for (size_t a = 0; a < mvars.size(); a++) {
            for (size_t b = a + 1; b < mvars.size(); b++) {
                adj[mvars[a]][mvars[b]] = 1;
                adj[mvars[b]][mvars[a]] = 1;
            }
        }
    }

    // classes of identical co-occurrence rows, ordered by their first variable
    std::map<vector<char>, vector<uint32_t> > classes;
    for (size_t i = 0; i < n; i++) {
        bool any = false;
        for (size_t j = 0; j < n; j++) any |= adj[i][j];
        if (!any) return false; // a variable that meets no other cannot be in a product
        classes[adj[i]].push_back(vars[i]);
    }
    if (classes.size() < 2) return false;
    vector<vector<uint32_t> > parts;
    for (auto& kv : classes) parts.push_back(kv.second);
    std::sort(parts.begin(), parts.end(),
              [](const vector<uint32_t>& a, const vector<uint32_t>& b) {
                  return a.front() < b.front();
              });

    // constants and the candidate product
    const BooleSet pset = poly.set();
    BoolePolynomial product(true, ring);
    for (size_t i = 0; i < parts.size(); i++) {
        BooleMonomial probe(ring);
        for (size_t j = 0; j < parts.size(); j++) {
            if (j != i) probe *= ring.variable(parts[j].front());
        }
        Lineral l;
        l.vars = parts[i];
        l.c = pset.owns(probe);
        BoolePolynomial f(l.c, ring);
        for (const uint32_t v : l.vars) f += ring.variable(v);
        product *= f;
        factors.push_back(l);
    }
    if (product != poly) {
        factors.clear();
        return false;
    }
    return true;
}

BoolePolynomial BLib::expand_linerals(const BoolePolyRing& ring,
                                      const vector<Lineral>& factors)
{
    BoolePolynomial product(true, ring);
    for (const Lineral& l : factors) {
        BoolePolynomial f(l.c, ring);
        for (const uint32_t v : l.vars) f += ring.variable(v);
        product *= f;
    }
    return product;
}

namespace {
// removes v from l if present; returns whether it was present
bool erase_var(Lineral& l, uint32_t v)
{
    auto it = std::lower_bound(l.vars.begin(), l.vars.end(), v);
    if (it == l.vars.end() || *it != v) return false;
    l.vars.erase(it);
    return true;
}
// toggles w in l (x + w + w = x)
void toggle_var(Lineral& l, uint32_t w)
{
    auto it = std::lower_bound(l.vars.begin(), l.vars.end(), w);
    if (it != l.vars.end() && *it == w) l.vars.erase(it);
    else l.vars.insert(it, w);
}
// after a substitution: drop factors equal to 1, report a factor equal to 0
bool normalise(vector<Lineral>& factors)
{
    vector<Lineral> out;
    for (const Lineral& l : factors) {
        if (l.vars.empty()) {
            if (!l.c) { factors.clear(); return false; }
            continue; // factor 1
        }
        out.push_back(l);
    }
    factors.swap(out);
    return true;
}
}

bool BLib::subst_lineral_const(vector<Lineral>& factors, uint32_t v, bool c)
{
    for (Lineral& l : factors) {
        if (erase_var(l, v)) l.c ^= c;
    }
    return normalise(factors);
}

bool BLib::subst_lineral_var(vector<Lineral>& factors, uint32_t v, uint32_t w, bool c)
{
    for (Lineral& l : factors) {
        if (erase_var(l, v)) {
            toggle_var(l, w);
            l.c ^= c;
        }
    }
    return normalise(factors);
}

bool BLib::factor_terms(const vector<VarVec>& terms, vector<Lineral>& factors)
{
    factors.clear();
    if (terms.empty()) return false;
    // membership is asked a handful of times (the constant term and one
    // probe per factor): a linear scan is cheaper than sorting the terms
    auto has_term = [&](const VarVec& t) {
        return std::find(terms.begin(), terms.end(), t) != terms.end();
    };
    size_t deg = 0;
    for (const VarVec& t : terms) deg = std::max(deg, t.size());
    if (deg == 0) return false;

    // the variables, sorted, and a direct var -> position table (the
    // variables of one equation are few, the table is sized by the largest)
    uint32_t max_var = 0;
    for (const VarVec& t : terms) for (const uint32_t v : t) max_var = std::max(max_var, v);
    vector<uint32_t> idx(max_var + 1, std::numeric_limits<uint32_t>::max());
    vector<uint32_t> vars;
    for (const VarVec& t : terms) {
        for (const uint32_t v : t) {
            if (idx[v] == std::numeric_limits<uint32_t>::max()) {
                idx[v] = 0;
                vars.push_back(v);
            }
        }
    }
    std::sort(vars.begin(), vars.end());
    const size_t n = vars.size();
    for (size_t i = 0; i < n; i++) idx[vars[i]] = i;

    if (deg == 1) {
        Lineral l;
        l.vars = vars;
        l.c = has_term(VarVec());
        factors.push_back(l);
        return true;
    }

    vector<vector<char> > adj(n, vector<char>(n, 0));
    for (const VarVec& t : terms) {
        if (t.size() < 2) continue;
        for (size_t a = 0; a < t.size(); a++) {
            for (size_t b = a + 1; b < t.size(); b++) {
                adj[idx[t[a]]][idx[t[b]]] = 1;
                adj[idx[t[b]]][idx[t[a]]] = 1;
            }
        }
    }
    std::map<vector<char>, vector<uint32_t> > classes;
    for (size_t i = 0; i < n; i++) {
        bool any = false;
        for (size_t j = 0; j < n; j++) any |= adj[i][j];
        if (!any) return false;
        classes[adj[i]].push_back(vars[i]);
    }
    if (classes.size() < 2) return false;
    vector<vector<uint32_t> > parts;
    for (auto& kv : classes) parts.push_back(kv.second);
    std::sort(parts.begin(), parts.end(),
              [](const vector<uint32_t>& a, const vector<uint32_t>& b) {
                  return a.front() < b.front();
              });
    for (size_t i = 0; i < parts.size(); i++) {
        VarVec probe;
        for (size_t j = 0; j < parts.size(); j++) {
            if (j != i) probe.push_back(parts[j].front());
        }
        std::sort(probe.begin(), probe.end());
        Lineral l;
        l.vars = parts[i];
        l.c = has_term(probe);
        factors.push_back(l);
    }
    // Verification without expanding: the parts are disjoint, so the
    // product's monomials are exactly "one variable from each part, or the
    // constant of that part": every input term must be of that shape, and
    // there must be as many terms as the product has.
    size_t expected = 1;
    for (const Lineral& l : factors) expected *= l.vars.size() + (l.c ? 1 : 0);
    if (expected != terms.size()) {
        factors.clear();
        return false;
    }
    vector<uint32_t>& part_of = idx; // reuse the table: var -> factor
    for (size_t i = 0; i < factors.size(); i++) {
        for (const uint32_t v : factors[i].vars) part_of[v] = i;
    }
    vector<char> seen(factors.size());
    for (const VarVec& t : terms) {
        std::fill(seen.begin(), seen.end(), 0);
        for (const uint32_t v : t) {
            const uint32_t i = part_of[v];
            if (seen[i]) { factors.clear(); return false; } // two variables of one part
            seen[i] = 1;
        }
        for (size_t i = 0; i < factors.size(); i++) {
            if (!seen[i] && !factors[i].c) { factors.clear(); return false; } // part contributes nothing but has no constant
        }
    }
    return true;
}

VarVec BLib::product_key(const vector<Lineral>& factors)
{
    vector<Lineral> sorted(factors);
    std::sort(sorted.begin(), sorted.end());
    VarVec key;
    for (const Lineral& l : sorted) {
        key.insert(key.end(), l.vars.begin(), l.vars.end());
        key.push_back(l.c ? std::numeric_limits<uint32_t>::max()
                          : std::numeric_limits<uint32_t>::max() - 1);
    }
    return key;
}

size_t BLib::product_size(const vector<Lineral>& factors)
{
    size_t n = 1;
    for (const Lineral& l : factors) n *= l.vars.size() + (l.c ? 1 : 0);
    return n;
}

bool BLib::normalize_product(vector<Lineral>& factors)
{
    std::sort(factors.begin(), factors.end());
    vector<Lineral> out;
    for (const Lineral& l : factors) {
        if (!out.empty() && out.back().vars == l.vars) {
            if (out.back().c != l.c) return false; // l * (l + 1) = 0
            continue;                              // l * l = l
        }
        out.push_back(l);
    }
    factors.swap(out);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// Dickson decomposition of a quadratic form over GF(2)
//
// With Q = x_i*x_j + x_i*A + x_j*B + C, where A, B, C do not contain x_i or
// x_j, Q = (x_i + B) * (x_j + A) + A*B + C: one product is peeled off and
// the rest, A*B + C, is a quadratic form in fewer variables (a*a = a turns
// the shared variables of A and B into linear terms). Repeating this until
// no quadratic monomial is left gives rank/2 products. The pivot is the
// edge x_i*x_j with the fewest fill-in monomials |A|*|B|, which also keeps
// the two linear forms short.
///////////////////////////////////////////////////////////////////////////////

bool BLib::quad_form_decompose(const BoolePolynomial& poly, vector<VarVec>& linerals,
                               VarVec& rest, bool& c)
{
    linerals.clear();
    rest.clear();
    c = poly.hasConstantPart();
    if (poly.deg() != 2) return false;

    std::map<uint32_t, std::set<uint32_t> > adj; // the quadratic part as a graph
    std::set<uint32_t> lin;
    for (const BooleMonomial& m : poly) {
        if (m.deg() == 1) {
            lin.insert(*m.begin());
        } else if (m.deg() == 2) {
            auto it = m.begin();
            const uint32_t a = *it;
            ++it;
            const uint32_t b = *it;
            adj[a].insert(b);
            adj[b].insert(a);
        }
    }
    auto toggle_edge = [&](uint32_t a, uint32_t b) {
        if (!adj[a].insert(b).second) adj[a].erase(b);
        if (!adj[b].insert(a).second) adj[b].erase(a);
    };
    while (true) {
        // the pivot edge: least fill-in
        uint32_t pi = 0, pj = 0;
        size_t best = std::numeric_limits<size_t>::max();
        for (const auto& kv : adj) {
            if (kv.second.empty()) continue;
            for (const uint32_t j : kv.second) {
                if (j < kv.first) continue;
                const size_t fill = (kv.second.size() - 1) * (adj[j].size() - 1);
                if (fill < best) { best = fill; pi = kv.first; pj = j; }
            }
        }
        if (best == std::numeric_limits<size_t>::max()) break;
        vector<uint32_t> A(adj[pi].begin(), adj[pi].end()); // neighbours of i, j removed
        vector<uint32_t> B(adj[pj].begin(), adj[pj].end());
        A.erase(std::remove(A.begin(), A.end(), pj), A.end());
        B.erase(std::remove(B.begin(), B.end(), pi), B.end());
        // l1 = x_i + B, l2 = x_j + A
        VarVec l1(B), l2(A);
        l1.push_back(pi);
        l2.push_back(pj);
        std::sort(l1.begin(), l1.end());
        std::sort(l2.begin(), l2.end());
        linerals.push_back(l1);
        linerals.push_back(l2);
        // x_i and x_j leave the form
        for (const uint32_t a : A) adj[a].erase(pi);
        for (const uint32_t b : B) adj[b].erase(pj);
        adj.erase(pi);
        adj.erase(pj);
        // + A*B
        for (const uint32_t a : A) {
            for (const uint32_t b : B) {
                if (a == b) {
                    if (!lin.insert(a).second) lin.erase(a);
                } else {
                    toggle_edge(a, b);
                }
            }
        }
    }
    rest.assign(lin.begin(), lin.end());
#ifndef NDEBUG
    {
        const BoolePolyRing& ring = poly.ring();
        BoolePolynomial check(c, ring);
        for (const uint32_t v : rest) check += BooleVariable(v, ring);
        for (size_t k = 0; k + 1 < linerals.size(); k += 2) {
            BoolePolynomial f(ring), g(ring);
            for (const uint32_t v : linerals[k]) f += BooleVariable(v, ring);
            for (const uint32_t v : linerals[k + 1]) g += BooleVariable(v, ring);
            check += f * g;
        }
        assert(check == poly);
    }
#endif
    return true;
}
