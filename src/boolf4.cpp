/*****************************************************************************
Copyright (C) 2026  Mate Soos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
***********************************************/

#include "boolf4.hpp"

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <m4ri/m4ri.h>

using namespace BLib;
using std::vector;

bool BoolF4::less(Mon a, Mon b)
{
    const int da = deg(a), db = deg(b);
    if (da != db) return da < db;
    const Mon d = a ^ b;
    if (d == 0) return false;
    // degrevlex: of two monomials of the same degree the larger is the one
    // that lacks the highest variable in which they differ
    const int h = 63 - __builtin_clzll(d);
    return (a >> h) & 1;
}

static bool greater(BoolF4::Mon a, BoolF4::Mon b) { return BoolF4::less(b, a); }

void BoolF4::normalize(Poly& p)
{
    std::sort(p.begin(), p.end(), greater);
    size_t w = 0;
    for (size_t i = 0; i < p.size();) {
        size_t j = i;
        while (j < p.size() && p[j] == p[i]) j++;
        if ((j - i) & 1) p[w++] = p[i];
        i = j;
    }
    p.resize(w);
}

BoolF4::Poly BoolF4::mul(const Poly& p, Mon u)
{
    Poly q;
    q.reserve(p.size());
    for (const Mon m : p) q.push_back(m | u);
    normalize(q); // x*m = m makes distinct terms coincide and cancel
    return q;
}

BoolF4::Poly BoolF4::add(const Poly& p, const Poly& q)
{
    Poly r;
    r.reserve(p.size() + q.size());
    size_t i = 0, j = 0;
    while (i < p.size() && j < q.size()) {
        if (p[i] == q[j]) { i++; j++; }
        else if (greater(p[i], q[j])) r.push_back(p[i++]);
        else r.push_back(q[j++]);
    }
    while (i < p.size()) r.push_back(p[i++]);
    while (j < q.size()) r.push_back(q[j++]);
    return r;
}

BoolF4::BoolF4(uint32_t nvars, const Options& opts) : n(nvars), opt(opts) {}

void BoolF4::add(const Poly& p_in)
{
    Poly p = p_in;
    normalize(p);
    if (p.empty()) return;
    add_to_basis(p);
}

void BoolF4::add_to_basis(const Poly& p)
{
    if (p.size() == 1 && p[0] == 0) { has_one = true; }
    const int h = G.size();
    G.push_back(p);
    LM.push_back(p[0]);
    alive.push_back(1);
    st.new_polys++;
    update_pairs(h);
}

// Gebauer-Moeller style pair update for the new element h
void BoolF4::update_pairs(int h)
{
    const Mon lh = LM[h];
    // pairs among the old basis whose lcm is a proper multiple of LM(h)
    // are superseded (chain criterion)
    vector<Pair> kept;
    kept.reserve(pairs.size());
    for (const Pair& pr : pairs) {
        if (pr.j >= 0 && (pr.lcm & lh) == lh && (LM[pr.i] | lh) != pr.lcm && (LM[pr.j] | lh) != pr.lcm) continue;
        kept.push_back(pr);
    }
    pairs.swap(kept);
    // new pairs (h, i)
    vector<Pair> fresh;
    for (int i = 0; i < h; i++) {
        if (!alive[i]) continue;
        const Mon l = LM[i] | lh;
        if (l == (LM[i] ^ lh)) continue; // disjoint leads: product criterion
        fresh.push_back(Pair{l, i, h, deg(l)});
    }
    // among the new pairs keep one per lcm, and drop those whose lcm is a
    // proper multiple of another new pair's lcm
    std::sort(fresh.begin(), fresh.end(), [](const Pair& a, const Pair& b) {
        return a.degree < b.degree || (a.degree == b.degree && a.lcm < b.lcm); });
    vector<Pair> chosen;
    for (const Pair& pr : fresh) {
        bool redundant = false;
        for (const Pair& c : chosen) {
            if ((pr.lcm & c.lcm) == c.lcm) { redundant = true; break; }
        }
        if (!redundant) chosen.push_back(pr);
    }
    for (const Pair& pr : chosen) pairs.push_back(pr);
    // variable pairs x * G[h] for x in LM(h): the field equations
    for (uint32_t v = 0; v < n; v++) {
        if (!((lh >> v) & 1)) continue;
        pairs.push_back(Pair{lh, h, -(int)v - 1, deg(lh) + 1});
    }
    // old elements whose lead is a multiple of the new lead are redundant
    for (int i = 0; i < h; i++) {
        if (alive[i] && (LM[i] & lh) == lh && LM[i] != lh) alive[i] = 0;
    }
}

// Rows: polynomials; columns: the monomials that occur, in decreasing
// order. Returns the reduced rows (nonzero), each sorted.
vector<BoolF4::Poly> BoolF4::echelon(vector<Poly>& rows, vector<Mon>& columns)
{
    std::unordered_map<Mon, size_t> col_of;
    columns.clear();
    for (const Poly& r : rows) for (const Mon m : r) columns.push_back(m);
    std::sort(columns.begin(), columns.end(), greater);
    columns.erase(std::unique(columns.begin(), columns.end()), columns.end());
    col_of.reserve(columns.size() * 2);
    for (size_t c = 0; c < columns.size(); c++) col_of[columns[c]] = c;

    st.rows += rows.size();
    st.cols_max = std::max<uint64_t>(st.cols_max, columns.size());
    mzd_t* M = mzd_init(rows.size(), columns.size());
    for (size_t r = 0; r < rows.size(); r++) {
        for (const Mon m : rows[r]) mzd_write_bit(M, r, col_of[m], 1);
    }
    const rci_t rank = mzd_echelonize_m4ri(M, 1, 0);
    vector<Poly> out;
    out.reserve(rank);
    for (rci_t r = 0; r < rank; r++) {
        Poly p;
        for (size_t c = 0; c < columns.size(); c++) {
            if (mzd_read_bit(M, r, c)) p.push_back(columns[c]);
        }
        if (!p.empty()) out.push_back(p);
    }
    mzd_free(M);
    return out;
}

void BoolF4::reduce_step(vector<Pair>& selected)
{
    // the rows: both halves of every S-pair, or the variable multiple
    vector<Poly> rows;
    std::unordered_set<Mon> done; // monomials that have a row with this lead
    for (const Pair& pr : selected) {
        if (pr.j >= 0) {
            rows.push_back(mul(G[pr.i], pr.lcm & ~LM[pr.i]));
            rows.push_back(mul(G[pr.j], pr.lcm & ~LM[pr.j]));
        } else {
            rows.push_back(mul(G[pr.i], (Mon)1 << (-pr.j - 1)));
        }
    }
    for (const Poly& r : rows) if (!r.empty()) done.insert(r[0]);
    // symbolic preprocessing: every other monomial that some lead divides
    // gets a reducer row
    std::unordered_set<Mon> seen;
    vector<Mon> todo;
    for (const Poly& r : rows) for (const Mon m : r) if (seen.insert(m).second) todo.push_back(m);
    while (!todo.empty()) {
        const Mon m = todo.back();
        todo.pop_back();
        if (done.count(m)) continue;
        for (size_t i = 0; i < G.size(); i++) {
            if (!alive[i]) continue;
            if ((m & LM[i]) != LM[i]) continue;
            Poly r = mul(G[i], m & ~LM[i]);
            done.insert(m);
            for (const Mon t : r) if (seen.insert(t).second) todo.push_back(t);
            rows.push_back(r);
            break;
        }
    }
    // the leads present before reduction: rows with a new lead afterwards
    // are the new basis elements
    std::unordered_set<Mon> old_leads;
    for (const Poly& r : rows) if (!r.empty()) old_leads.insert(r[0]);
    vector<Mon> columns;
    const uint64_t cells = (uint64_t)rows.size() * seen.size();
    if (cells > opt.maxCells) { st.budget_exhausted = true; return; }
    vector<Poly> red = echelon(rows, columns);
    st.steps++;
    size_t added = 0;
    for (const Poly& p : red) {
        if (old_leads.count(p[0])) continue;
        add_to_basis(p);
        added++;
        if (has_one) return;
    }
    st.zero_reductions += selected.size() - std::min(added, selected.size());
    if (opt.verbosity >= 2) {
        std::cout << "c [f4] degree " << selected[0].degree << " pairs " << selected.size()
                  << " rows " << rows.size() << " cols " << columns.size() << " new " << added
                  << " basis " << G.size() << std::endl;
    }
}

// Reduces the tails of the live basis elements against each other's leads
// (their leads stay, so the pending pairs stay valid): shorter polynomials
// give smaller matrices in the following steps.
void BoolF4::tail_reduce()
{
    vector<size_t> idx;
    vector<Poly> rows;
    for (size_t i = 0; i < G.size(); i++) if (alive[i]) { idx.push_back(i); rows.push_back(G[i]); }
    if (rows.size() < 2) return;
    vector<Mon> columns;
    const uint64_t cells = (uint64_t)rows.size() * 0; (void)cells;
    vector<Poly> red = echelon(rows, columns);
    std::unordered_map<Mon, size_t> by_lead;
    for (const size_t i : idx) by_lead[LM[i]] = i;
    for (const Poly& p : red) {
        auto it = by_lead.find(p[0]);
        if (it != by_lead.end()) G[it->second] = p;
    }
}

void BoolF4::interreduce()
{
    vector<Poly> rows;
    for (size_t i = 0; i < G.size(); i++) if (alive[i]) rows.push_back(G[i]);
    vector<Mon> columns;
    vector<Poly> red = echelon(rows, columns);
    G.clear(); LM.clear(); alive.clear();
    for (const Poly& p : red) { G.push_back(p); LM.push_back(p[0]); alive.push_back(1); }
    // minimal: drop elements whose lead is a multiple of another lead
    for (size_t i = 0; i < G.size(); i++) {
        for (size_t j = 0; j < G.size(); j++) {
            if (i != j && alive[j] && (LM[i] & LM[j]) == LM[j] && (LM[i] != LM[j] || j < i)) { alive[i] = 0; break; }
        }
    }
}

vector<BoolF4::Poly> BoolF4::run()
{
    while (!pairs.empty() && !has_one) {
        int dmin = pairs[0].degree;
        for (const Pair& pr : pairs) dmin = std::min(dmin, pr.degree);
        if ((uint32_t)dmin > opt.maxDeg || st.rows > opt.maxRows) { st.budget_exhausted = true; break; }
        st.max_deg = std::max<uint32_t>(st.max_deg, dmin);
        vector<Pair> selected, rest;
        for (const Pair& pr : pairs) (pr.degree == dmin ? selected : rest).push_back(pr);
        pairs.swap(rest);
        reduce_step(selected);
        if (st.budget_exhausted) break;
        if (opt.tailReduce) tail_reduce();
    }
    if (has_one) {
        Poly one(1, 0);
        return vector<Poly>(1, one);
    }
    interreduce();
    vector<Poly> out;
    for (size_t i = 0; i < G.size(); i++) if (alive[i]) out.push_back(G[i]);
    return out;
}
