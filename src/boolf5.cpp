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

#include "boolf5.hpp"

#include <algorithm>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <m4ri/m4ri.h>

using namespace BLib;
using std::vector;

static bool greater(BoolF4::Mon a, BoolF4::Mon b) { return BoolF4::less(b, a); }

void BoolF5::add(const Poly& p_in)
{
    Poly p = p_in;
    BoolF4::normalize(p);
    if (p.empty()) return;
    F.push_back(p);
}

namespace {
// all squarefree monomials of degree k over n variables
void monomials_of_degree(uint32_t n, int k, vector<BoolF4::Mon>& out)
{
    out.clear();
    if (k == 0) { out.push_back(0); return; }
    if ((uint32_t)k > n) return;
    // Gosper's hack over n bits
    BoolF4::Mon v = ((BoolF4::Mon)1 << k) - 1;
    const BoolF4::Mon limit = (n == 64) ? ~(BoolF4::Mon)0 : (((BoolF4::Mon)1 << n) - 1);
    while (true) {
        out.push_back(v);
        const BoolF4::Mon t = v | (v - 1);
        const BoolF4::Mon w = (t + 1) | (((~t & -~t) - 1) >> (__builtin_ctzll(v) + 1));
        if (w > limit || w < v) break;
        v = w;
    }
}
}

vector<BoolF4::Poly> BoolF5::run()
{
    // generators in the order of increasing degree, then input order
    vector<size_t> order(F.size());
    for (size_t i = 0; i < F.size(); i++) order[i] = i;
    vector<int> degs(F.size());
    for (size_t i = 0; i < F.size(); i++) {
        int d = 0;
        for (const Mon m : F[i]) d = std::max(d, BoolF4::deg(m));
        degs[i] = d;
    }
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return degs[a] < degs[b]; });
    vector<Poly> G;
    for (const size_t i : order) { G.push_back(F[i]); Fdeg.push_back(degs[i]); }
    const size_t m = G.size();
    const uint32_t ngroups = std::max<uint32_t>(1, std::min<uint32_t>(opt.groups, m));
    vector<size_t> group_of(m);
    for (size_t i = 0; i < m; i++) group_of[i] = i * ngroups / m;

    // leads of the degree-(d) rows of the generators of groups < g, per degree
    // prev_leads[d][g] : set of leading monomials (of exact degree d)
    std::unordered_map<int, vector<std::unordered_set<Mon> > > leads_by_deg;

    vector<Poly> basis; // all reduced rows found so far (leads distinct)
    std::unordered_set<Mon> basis_leads;
    int mindeg = Fdeg.empty() ? 2 : Fdeg[0];
    for (int d = mindeg; (uint32_t)d <= opt.maxDeg; d++) {
        st.max_deg = d;
        // columns: all squarefree monomials of degree <= d
        vector<Mon> columns;
        for (int k = d; k >= 0; k--) {
            vector<Mon> mons;
            monomials_of_degree(n, k, mons);
            columns.insert(columns.end(), mons.begin(), mons.end());
        }
        std::sort(columns.begin(), columns.end(), greater);
        std::unordered_map<Mon, size_t> col_of;
        col_of.reserve(columns.size() * 2);
        for (size_t c = 0; c < columns.size(); c++) col_of[columns[c]] = c;
        st.cols_max = std::max<uint64_t>(st.cols_max, columns.size());

        vector<std::unordered_set<Mon> >& leads_d = leads_by_deg[d];
        leads_d.assign(ngroups + 1, std::unordered_set<Mon>());

        mzd_t* E = nullptr; // RREF of the rows of the groups processed so far
        size_t erows = 0;
        vector<Poly> degree_rows; // the reduced rows of this degree (to become basis elements)
        for (uint32_t g = 0; g < ngroups; g++) {
            // rows of this group: m*f_i for deg(m) = d - deg(f_i), F5 criterion
            // against the leads of degree d - deg(f_i) of the groups < g
            vector<std::pair<size_t, Mon> > desc;
            for (size_t i = 0; i < m; i++) {
                if (group_of[i] != g) continue;
                const int k = d - Fdeg[i];
                if (k < 0) continue;
                vector<Mon> mult;
                monomials_of_degree(n, k, mult);
                const std::unordered_set<Mon>* crit = nullptr;
                auto it = leads_by_deg.find(k);
                if (it != leads_by_deg.end() && g < it->second.size()) crit = &it->second[g];
                for (const Mon u : mult) {
                    if (crit && crit->count(u)) { st.rows_pruned++; continue; }
                    desc.push_back(std::make_pair(i, u));
                }
            }
            if (desc.empty()) { leads_d[g + 1] = leads_d[g]; continue; }
            const uint64_t cells = (uint64_t)(erows + desc.size()) * columns.size();
            if (cells > opt.maxCells) {
                if (opt.verbosity >= 1) {
                    std::cout << "c [f5] degree " << d << ": " << (erows + desc.size()) << " rows x "
                              << columns.size() << " columns exceed the cell budget" << std::endl;
                }
                st.budget_exhausted = true;
                break;
            }
            // matrix [E; new rows], full echelon: E's pivots come first, so the
            // new rows are reduced by them and among themselves
            mzd_t* M = mzd_init(erows + desc.size(), columns.size());
            if (E) { mzd_copy(M, E); mzd_free(E); E = nullptr; }
            for (size_t r = 0; r < desc.size(); r++) {
                const Poly p = BoolF4::mul(G[desc[r].first], desc[r].second);
                for (const Mon mm : p) mzd_write_bit(M, erows + r, col_of[mm], 1);
            }
            st.rows += desc.size();
            const rci_t rank = mzd_echelonize_m4ri(M, 1, 0);
            st.zero_rows += (erows + desc.size()) - rank;
            // read the rows back, record leads
            const size_t words = (columns.size() + 63) / 64;
            leads_d[g + 1] = leads_d[g];
            degree_rows.clear();
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
                if (p.empty()) continue;
                if (BoolF4::deg(p[0]) == d) leads_d[g + 1].insert(p[0]);
                degree_rows.push_back(p);
            }
            E = mzd_init(rank, columns.size());
            for (rci_t r = 0; r < rank; r++) mzd_copy_row(E, r, M, r);
            mzd_free(M);
            erows = rank;
        }
        if (E) mzd_free(E);
        if (opt.verbosity >= 2) {
            std::cout << "c [f5] degree " << d << " cols " << columns.size() << " rows " << erows
                      << " pruned so far " << st.rows_pruned << " zero rows so far " << st.zero_rows << std::endl;
        }
        // the rows of this degree supersede the basis: they contain every
        // earlier element multiplied up (and reduced), so take them as the
        // current basis
        basis = degree_rows;
        if (st.budget_exhausted) break;
        // finished? every variable determined, or the ideal is the whole ring
        bool one = false; size_t linear = 0;
        for (const Poly& p : basis) {
            if (p.size() == 1 && p[0] == 0) one = true;
            if (BoolF4::deg(p[0]) == 1) linear++;
        }
        if (one || linear >= n) break;
        if (degree_rows.empty()) break;
    }
    // minimal reduced basis: drop elements whose lead is divisible by another lead
    vector<Poly> out;
    vector<Mon> lm;
    for (const Poly& p : basis) lm.push_back(p[0]);
    for (size_t i = 0; i < basis.size(); i++) {
        bool redundant = false;
        for (size_t j = 0; j < basis.size(); j++) {
            if (i != j && (lm[i] & lm[j]) == lm[j] && (lm[i] != lm[j] || j < i)) { redundant = true; break; }
        }
        if (!redundant) out.push_back(basis[i]);
    }
    return out;
}
