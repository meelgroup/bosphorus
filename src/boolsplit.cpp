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

#include "boolsplit.hpp"
#include "boolf5.hpp"

#include <algorithm>
#include <iostream>
#include <set>
#include <unordered_map>

using namespace BLib;
using std::vector;

namespace {

bool is_one(const BoolSplit::Poly& p) { return p.size() == 1 && p[0] == 0; }

// the variables occurring in g, as a mask
BoolSplit::Mon used_vars(const vector<BoolSplit::Poly>& g)
{
    BoolSplit::Mon u = 0;
    for (const BoolSplit::Poly& p : g) for (const BoolSplit::Mon m : p) u |= m;
    return u;
}

// renumbers the variables of p through idx[old] = new (idx < 0: the
// variable must not occur). Order-preserving renumberings keep the
// degrevlex order of the monomials; the polynomial is normalised anyway.
BoolSplit::Poly remap(const BoolSplit::Poly& p, const vector<int>& idx)
{
    BoolSplit::Poly q;
    q.reserve(p.size());
    for (BoolSplit::Mon m : p) {
        BoolSplit::Mon r = 0;
        while (m) {
            const int v = __builtin_ctzll(m);
            m &= m - 1;
            r |= (BoolSplit::Mon)1 << idx[v];
        }
        q.push_back(r);
    }
    BoolF4::normalize(q);
    return q;
}

}

void BoolSplit::add(const Poly& p_in)
{
    Poly p = p_in;
    BoolF4::normalize(p);
    if (p.empty()) return;
    gens.push_back(p);
}

vector<BoolSplit::Poly> BoolSplit::run()
{
    Result r = solve(gens, n, 0, true);
    st.complete = r.complete;
    if (r.has_one) return vector<Poly>(1, Poly(1, 0));
    return r.polys;
}

// One engine run on g over nv variables. `exhausted` tells whether it
// stopped on a budget (the result is then a partial basis: still sound).
BoolSplit::Result BoolSplit::engine_run(const vector<Poly>& g, uint32_t nv, bool& exhausted)
{
    Result r;
    exhausted = false;
    st.branches++;
    for (const Poly& p : g) {
        if (is_one(p)) { r.has_one = true; st.unsat_branches++; return r; }
    }
    if (g.empty()) return r;
    const uint64_t rows_left = st.rows >= opt.maxRows ? 0 : opt.maxRows - st.rows;
    if (rows_left == 0) { exhausted = true; r.complete = false; return r; }

    vector<Poly> basis;
    if (opt.engine == 2) {
        BoolF5::Options o;
        o.maxDeg = opt.maxDeg;
        o.maxCells = opt.maxCells;
        o.groups = opt.f5groups;
        o.verbosity = opt.verbosity;
        BoolF5 f5(nv, o);
        for (const Poly& p : g) f5.add(p);
        basis = f5.run();
        st.rows += f5.stats().rows;
        exhausted = f5.stats().budget_exhausted;
    } else {
        BoolF4::Options o;
        o.maxDeg = opt.maxDeg;
        o.maxRows = rows_left;
        o.maxCells = opt.maxCells;
        o.tailReduce = false;
        o.verbosity = opt.verbosity;
        BoolF4 f4(nv, o);
        for (const Poly& p : g) f4.add(p);
        basis = f4.run();
        st.rows += f4.stats().rows;
        exhausted = f4.stats().budget_exhausted;
    }
    r.complete = !exhausted;
    for (const Poly& p : basis) {
        if (is_one(p)) { r.has_one = true; break; }
    }
    if (r.has_one) {
        st.unsat_branches++;
        r.polys.clear();
        return r;
    }
    r.polys = basis;
    // solved: a linear member per variable in use, with distinct leads
    Mon leads = 0;
    for (const Poly& p : basis) {
        if (BoolF4::deg(p[0]) == 1) leads |= p[0];
    }
    if (leads == used_vars(g)) st.solved_branches++;
    return r;
}

BoolSplit::Result BoolSplit::combine(const Result& r0, const Result& r1, uint32_t x, uint32_t condLen)
{
    Result out;
    out.complete = r0.complete && r1.complete;
    const Mon xm = (Mon)1 << x;
    if (r0.has_one && r1.has_one) { out.has_one = true; return out; }
    if (r0.has_one || r1.has_one) {
        // x is forced: the other branch's ideal is the ideal itself
        const Result& live = r0.has_one ? r1 : r0;
        Poly fx;
        fx.push_back(xm);
        if (r0.has_one) fx.push_back(0); // x = 1
        out.polys.push_back(fx);
        out.polys.insert(out.polys.end(), live.polys.begin(), live.polys.end());
        return out;
    }
    // both consistent (as far as known): pair by leading monomial
    std::unordered_map<Mon, size_t> lead1;
    for (size_t i = 0; i < r1.polys.size(); i++) lead1[r1.polys[i][0]] = i;
    vector<char> used1(r1.polys.size(), 0);
    std::set<Poly> seen;
    auto emit = [&](const Poly& p) {
        if (p.empty()) return;
        if (seen.insert(p).second) out.polys.push_back(p);
    };
    for (const Poly& f0 : r0.polys) {
        auto it = lead1.find(f0[0]);
        if (it == lead1.end()) {
            // only under x = 0: (x + 1) * f0
            if (f0.size() <= condLen) emit(BoolF4::add(f0, BoolF4::mul(f0, xm)));
            continue;
        }
        used1[it->second] = 1;
        const Poly& f1 = r1.polys[it->second];
        if (f0 == f1) { emit(f0); continue; }
        // f0 + x*(f0 + f1): f0 when x = 0, f1 when x = 1
        emit(BoolF4::add(f0, BoolF4::mul(BoolF4::add(f0, f1), xm)));
    }
    for (size_t i = 0; i < r1.polys.size(); i++) {
        if (used1[i]) continue;
        const Poly& f1 = r1.polys[i];
        if (f1.size() <= condLen) emit(BoolF4::mul(f1, xm)); // only under x = 1: x * f1
    }
    return out;
}

BoolSplit::Result BoolSplit::solve(const vector<Poly>& g, uint32_t nv, uint32_t depth, bool try_engine)
{
    bool exhausted = true;
    Result r;
    if (try_engine) {
        r = engine_run(g, nv, exhausted);
        if (!exhausted || r.has_one) return r;
    } else {
        st.skipped++;
        r.complete = false;
    }
    if (depth >= opt.maxDepth || st.rows >= opt.maxRows) {
        st.budget_branches++;
        return r;
    }
    st.max_depth = std::max(st.max_depth, depth + 1);

    // the variable in the most monomials; ties: the lowest index
    vector<uint64_t> occ(nv, 0);
    for (const Poly& p : g) {
        for (Mon m : p) {
            while (m) { occ[__builtin_ctzll(m)]++; m &= m - 1; }
        }
    }
    uint32_t x = 0;
    for (uint32_t v = 1; v < nv; v++) if (occ[v] > occ[x]) x = v;
    if (occ[x] == 0) { st.budget_branches++; return r; } // no variable at all (cannot happen)

    // the two specialisations, over the variables other than x renumbered
    // 0..nv-2 (order-preserving)
    vector<int> down(nv, -1), up;
    for (uint32_t v = 0; v < nv; v++) {
        if (v == x) continue;
        down[v] = up.size();
        up.push_back(v);
    }
    const Mon xm = (Mon)1 << x;
    vector<Poly> g0, g1;
    g0.reserve(g.size());
    g1.reserve(g.size());
    for (const Poly& p : g) {
        Poly p0, p1;
        p0.reserve(p.size());
        p1.reserve(p.size());
        for (const Mon m : p) {
            if (m & xm) p1.push_back(m & ~xm); // x = 1: the monomial loses x
            else { p0.push_back(m); p1.push_back(m); }
        }
        BoolF4::normalize(p1);
        if (!p0.empty()) g0.push_back(remap(p0, down));
        if (!p1.empty()) g1.push_back(remap(p1, down));
    }
    if (opt.verbosity >= 1) {
        std::cout << "c [gb-split] depth " << depth + 1 << ": splitting on variable " << x
                  << " (" << nv - 1 << " variables left)" << std::endl;
    }
    // the more constrained branch first is no cheaper, so the order is
    // fixed; if the first branch had to split, the second is split right
    // away (same size, same degree of regularity for a random system)
    Result r0 = solve(g0, nv - 1, depth + 1, true);
    Result r1 = solve(g1, nv - 1, depth + 1, !r0.split);
    vector<int> back(nv - 1);
    for (uint32_t k = 0; k + 1 < nv; k++) back[k] = up[k];
    for (Poly& p : r0.polys) p = remap(p, back);
    for (Poly& p : r1.polys) p = remap(p, back);
    Result c = combine(r0, r1, x, opt.condLen);
    c.split = true;
    // the members found before the split hold as well (a partial basis of
    // the ideal); the leaves' facts are what solves the system
    for (const Poly& p : r.polys) c.polys.push_back(p);
    return c;
}
