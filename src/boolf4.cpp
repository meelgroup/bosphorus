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
#include <iomanip>
#include "time_mem.h"

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
    if (u == 0) return p;
    const int du = deg(u);
    if (du > 3) {
        Poly q;
        q.reserve(p.size());
        for (const Mon m : p) q.push_back(m | u);
        normalize(q); // x*m = m makes distinct terms coincide and cancel
        return q;
    }
    // The monomials with the same overlap m & u keep their degrevlex order
    // when the rest of u is added to all of them (same degree change, same
    // highest differing bit), so the product is a merge of at most 2^deg(u)
    // sorted lists, with cancellation, instead of a sort (a fifth of a run).
    uint32_t bits[3];
    {
        Mon t = u;
        for (int k = 0; k < du; k++) { bits[k] = __builtin_ctzll(t); t &= t - 1; }
    }
    Poly groups[8];
    for (const Mon m : p) {
        unsigned g = 0;
        for (int k = 0; k < du; k++) g |= ((m >> bits[k]) & 1) << k;
        groups[g].push_back(m | u);
    }
    Poly out;
    bool have = false;
    for (unsigned g = 0; g < (1u << du); g++) {
        if (groups[g].empty()) continue;
        if (!have) { out.swap(groups[g]); have = true; }
        else out = add(out, groups[g]);
    }
    return out;
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
    F0.push_back(p);
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
        Poly().swap(rows[r]); // the matrix holds it now: free the memory before the elimination
    }
    vector<Poly>().swap(rows);
    col_of.clear();
    const rci_t rank = mzd_echelonize_m4ri(M, 1, 0);
    vector<Poly> out;
    out.reserve(rank);
    const size_t words = (columns.size() + 63) / 64;
    for (rci_t r = 0; r < rank; r++) {
        Poly p;
        const word* row = mzd_row(M, r);
        for (size_t w = 0; w < words; w++) {
            word x = row[w];
            while (x) {
                const size_t c = w * 64 + __builtin_ctzll(x);
                x &= x - 1;
                if (c < columns.size()) p.push_back(columns[c]);
            }
        }
        if (!p.empty()) out.push_back(p);
    }
    mzd_free(M);
    return out;
}

void BoolF4::reduce_step(vector<Pair>& selected)
{
    // Rows are kept as descriptors (basis element, multiplier) and the
    // polynomials are generated twice: once to collect the columns, once
    // to fill the matrix row by row. Storing all row polynomials at once
    // took 3-4 times the memory of the matrix itself.
    struct RowDesc { int i; Mon u; };
    vector<RowDesc> rows;
    MonSet done; // monomials that have a row with this lead
    MonSet seen; // all monomials of all rows: the columns
    vector<Mon> todo, columns; // columns: every monomial seen, in order of discovery
    auto scan = [&](const Poly& r) {
        if (r.empty()) return;
        done.insert(r[0]);
        for (const Mon t : r) if (seen.insert(t)) { todo.push_back(t); columns.push_back(t); }
    };
    for (const Pair& pr : selected) {
        if (pr.j >= 0) {
            rows.push_back(RowDesc{pr.i, pr.lcm & ~LM[pr.i]});
            rows.push_back(RowDesc{pr.j, pr.lcm & ~LM[pr.j]});
        } else {
            rows.push_back(RowDesc{pr.i, (Mon)1 << (-pr.j - 1)});
        }
    }
    // rows and columns only grow, so the matrix is known to be over the
    // cell budget as soon as their product is: stop before the (long)
    // preprocessing of a step that will not be run
    auto over_budget = [&]() { return (uint64_t)rows.size() * seen.size() > opt.maxCells; };
    for (const RowDesc& d : rows) {
        if (over_budget()) break;
        scan(mul(G[d.i], d.u));
    }
    // symbolic preprocessing: every other monomial that some lead divides
    // gets a reducer row
    while (!todo.empty() && !over_budget()) {
        const Mon m = todo.back();
        todo.pop_back();
        if (done.count(m)) continue;
        for (size_t i = 0; i < G.size(); i++) {
            if (!alive[i]) continue;
            if ((m & LM[i]) != LM[i]) continue;
            const RowDesc d{(int)i, m & ~LM[i]};
            rows.push_back(d);
            scan(mul(G[i], d.u));
            break;
        }
    }
    const size_t nrows = rows.size();
    if (over_budget()) {
        if (opt.verbosity >= 1) {
            std::cout << "c [f4] degree " << selected[0].degree << ": " << nrows << " rows x "
                      << seen.size() << " columns (or more) exceed the cell budget T: "
                      << std::fixed << std::setprecision(2) << cpuTime() << std::endl;
        }
        st.budget_exhausted = true;
        return;
    }
    // the columns, in decreasing order, and the matrix
    vector<Mon>().swap(todo);
    seen.clear();
    std::sort(columns.begin(), columns.end(), greater);
    MonSet col_of;
    col_of.reserve(columns.size());
    col_of.with_values(true);
    for (size_t c = 0; c < columns.size(); c++) col_of.insert(columns[c], c);
    st.rows += nrows;
    st.cols_max = std::max<uint64_t>(st.cols_max, columns.size());
    mzd_t* M = mzd_init(nrows, columns.size());
    for (size_t r = 0; r < nrows; r++) {
        const Poly p = mul(G[rows[r].i], rows[r].u);
        for (const Mon m : p) mzd_write_bit(M, r, col_of.get(m), 1);
    }
    vector<RowDesc>().swap(rows);
    col_of.clear();
    const double elim_start = cpuTime();
    const rci_t rank = mzd_echelonize_m4ri(M, 1, 0);
    const double elim_time = cpuTime() - elim_start;
    st.steps++;
    // rows with a lead that no row had before reduction are the new basis elements
    size_t added = 0;
    const size_t words = (columns.size() + 63) / 64;
    for (rci_t r = 0; r < rank; r++) {
        const word* row = mzd_row(M, r);
        Poly p;
        for (size_t w = 0; w < words; w++) {
            word x = row[w];
            while (x) {
                const size_t c = w * 64 + __builtin_ctzll(x);
                x &= x - 1;
                if (c < columns.size()) p.push_back(columns[c]);
            }
        }
        if (p.empty() || done.count(p[0])) continue;
        add_to_basis(p);
        added++;
        if (has_one) break;
    }
    mzd_free(M);
    st.zero_reductions += selected.size() - std::min(added, selected.size());
    if (opt.verbosity >= 2) {
        std::cout << "c [f4] degree " << selected[0].degree << " pairs " << selected.size()
                  << " rows " << nrows << " cols " << columns.size() << " new " << added
                  << " basis " << G.size() << " T: " << std::fixed << std::setprecision(2)
                  << cpuTime() << " (elim " << std::setprecision(2) << elim_time << ")" << std::endl;
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

// The linear basis members fix every variable of the generators: the
// basis is then {x_i + c_i} if the assignment satisfies the generators
// and {1} otherwise, and the pending pairs (all reducing to zero) can be
// skipped. On a random quadratic system half of the rows went into the
// steps after the solving one.
bool BoolF4::solved()
{
    Mon used = 0, leads = 0;
    for (const Poly& p : F0) for (const Mon m : p) used |= m;
    for (size_t i = 0; i < G.size(); i++) {
        if (alive[i] && deg(LM[i]) == 1) leads |= LM[i];
    }
    if (leads != used) return false;
    // the assignment: x = c from the linear members, in decreasing lead
    // order so that the tail of each is already known (tails hold smaller
    // variables only after interreduction; solve by substitution otherwise)
    vector<Poly> lin;
    for (size_t i = 0; i < G.size(); i++) {
        if (alive[i] && deg(LM[i]) == 1) lin.push_back(G[i]);
    }
    vector<Poly> red = echelon(lin, columns_scratch);
    Mon ones = 0; // variables equal to 1
    for (const Poly& p : red) {
        if (p.size() > 2 || deg(p[0]) != 1) return false; // not a plain assignment: keep going
        if (p.size() == 2) {
            if (p[1] != 0) return false;
            ones |= p[0];
        }
    }
    for (const Poly& p : F0) {
        bool val = false;
        for (const Mon m : p) if ((m & ones) == m) val = !val;
        if (val) { has_one = true; return true; }
    }
    // the basis is the assignment itself
    G.clear(); LM.clear(); alive.clear();
    for (const Poly& p : red) { G.push_back(p); LM.push_back(p[0]); alive.push_back(1); }
    pairs.clear();
    return true;
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
        if (solved()) break;
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
