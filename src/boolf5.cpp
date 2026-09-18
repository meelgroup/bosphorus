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
#include <iomanip>
#include "time_mem.h"

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

    // leads_by_deg[d][g]: leading monomials of the rows of degree d of the
    // generators of the groups before g (the F5 criterion for multipliers
    // of degree d)
    std::unordered_map<int, vector<std::unordered_set<Mon> > > leads_by_deg;

    vector<Poly> basis; // all reduced rows found so far (leads distinct)
    std::unordered_set<Mon> mutant_done; // leads of the mutants already multiplied up
    std::unordered_set<Mon> prev_leads;  // leads of the rows of the previous degree
    // the variables that occur: a system is solved when each has a linear member
    Mon used = 0;
    for (const Poly& p : G) for (const Mon mm : p) used |= mm;
    const size_t n_used = __builtin_popcountll(used);
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

        // the rows of every group, pruned by the F5 criterion (which only
        // consults leads recorded at earlier degrees, so all groups can be
        // generated up front); one matrix holds the whole degree and is
        // echelonized in place group by group through M4RI windows
        vector<vector<std::pair<size_t, Mon> > > descs(ngroups);
        size_t total_rows = 0;
        for (uint32_t g = 0; g < ngroups; g++) {
            for (size_t i = 0; i < m; i++) {
                if (group_of[i] != g) continue;
                const int kmax = d - Fdeg[i];
                if (kmax < 0) continue;
                for (int k = 0; k <= kmax; k++) {
                    vector<Mon> mult;
                    monomials_of_degree(n, k, mult);
                    const std::unordered_set<Mon>* crit = nullptr;
                    auto it = leads_by_deg.find(k);
                    if (it != leads_by_deg.end() && g < it->second.size()) crit = &it->second[g];
                    for (const Mon u : mult) {
                        if (crit && crit->count(u)) { st.rows_pruned++; continue; }
                        descs[g].push_back(std::make_pair(i, u));
                    }
                }
            }
            total_rows += descs[g].size();
        }
        const uint64_t cells = (uint64_t)total_rows * columns.size();
        if (cells > opt.maxCells) {
            if (opt.verbosity >= 1) {
                std::cout << "c [f5] degree " << d << ": " << total_rows << " rows x "
                          << columns.size() << " columns exceed the cell budget" << std::endl;
            }
            st.budget_exhausted = true;
            break;
        }
        mzd_t* M = mzd_init(total_rows, columns.size());
        size_t erows = 0; // rows of the reduced echelon form so far (top of M)
        vector<Poly> degree_rows;
        const size_t words = (columns.size() + 63) / 64;
        for (uint32_t g = 0; g < ngroups; g++) {
            if (descs[g].empty()) { leads_d[g + 1] = leads_d[g]; continue; }
            // the rows of this group go right after the echelon rows
            for (size_t r = 0; r < descs[g].size(); r++) {
                const size_t row = erows + r;
                mzd_row_clear_offset(M, row, 0);
                const Poly p = BoolF4::mul(G[descs[g][r].first], descs[g][r].second);
                for (const Mon mm : p) mzd_write_bit(M, row, col_of[mm], 1);
            }
            st.rows += descs[g].size();
            mzd_t* W = mzd_init_window(M, 0, 0, erows + descs[g].size(), columns.size());
            const rci_t rank = mzd_echelonize_m4ri(W, 1, 0);
            mzd_free_window(W);
            st.zero_rows += (erows + descs[g].size()) - rank;
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
                leads_d[g + 1].insert(p[0]);
                degree_rows.push_back(p);
            }
            erows = rank;
        }
        mzd_free(M);
        if (opt.verbosity >= 2) {
            std::cout << "c [f5] degree " << d << " cols " << columns.size() << " rows " << erows
                      << " pruned so far " << st.rows_pruned << " zero rows so far " << st.zero_rows
                      << " T: " << std::fixed << std::setprecision(2) << cpuTime() << std::endl;
        }
        // finished? every variable determined, or the ideal is the whole ring
        auto finished = [&]() {
            bool one = false;
            size_t linear = 0;
            for (const Poly& p : degree_rows) {
                if (p.size() == 1 && p[0] == 0) one = true;
                if (BoolF4::deg(p[0]) == 1) linear++;
            }
            return one || linear >= n_used || degree_rows.size() >= columns.size();
        };
        // Mutants (MutantXL): rows of degree below d are combinations that
        // fell in degree; multiplied up to degree d again they give what
        // the plain Macaulay matrix of degree d lacks (F4 gets this from
        // its second step of the same degree). Repeated until no row of
        // lower degree is new. Only leads are tracked: a mutant is
        // multiplied once.
        while (!st.budget_exhausted && !finished()) {
            // a row of lower degree whose lead the previous degree did not
            // have: the span grew below degree d
            vector<Poly> mutants;
            for (const Poly& p : degree_rows) {
                if (BoolF4::deg(p[0]) < d && !prev_leads.count(p[0]) && mutant_done.insert(p[0]).second) mutants.push_back(p);
            }
            if (mutants.empty()) break;
            vector<std::pair<size_t, Mon> > mdescs;
            for (size_t i = 0; i < mutants.size(); i++) {
                const int kmax = d - BoolF4::deg(mutants[i][0]);
                for (int k = 1; k <= kmax; k++) {
                    vector<Mon> mult;
                    monomials_of_degree(n, k, mult);
                    for (const Mon u : mult) mdescs.push_back(std::make_pair(i, u));
                }
            }
            const uint64_t mcells = (uint64_t)(erows + mdescs.size()) * columns.size();
            if (mcells > opt.maxCells) {
                if (opt.verbosity >= 1) {
                    std::cout << "c [f5] degree " << d << " mutants: " << erows + mdescs.size() << " rows x "
                              << columns.size() << " columns exceed the cell budget" << std::endl;
                }
                st.budget_exhausted = true;
                break;
            }
            mzd_t* M2 = mzd_init(erows + mdescs.size(), columns.size());
            for (size_t r = 0; r < degree_rows.size(); r++) {
                for (const Mon mm : degree_rows[r]) mzd_write_bit(M2, r, col_of[mm], 1);
            }
            for (size_t r = 0; r < mdescs.size(); r++) {
                const Poly p = BoolF4::mul(mutants[mdescs[r].first], mdescs[r].second);
                for (const Mon mm : p) mzd_write_bit(M2, erows + r, col_of[mm], 1);
            }
            st.rows += mdescs.size();
            const rci_t rank = mzd_echelonize_m4ri(M2, 1, 0);
            st.zero_rows += (erows + mdescs.size()) - rank;
            degree_rows.clear();
            for (rci_t r = 0; r < rank; r++) {
                const word* row = mzd_row(M2, r);
                Poly p;
                for (size_t w = 0; w < words; w++) {
                    word x = row[w];
                    while (x) {
                        const size_t c = w * 64 + __builtin_ctzll(x);
                        x &= x - 1;
                        if (c < columns.size()) p.push_back(columns[c]);
                    }
                }
                if (!p.empty()) degree_rows.push_back(p);
            }
            mzd_free(M2);
            if (opt.verbosity >= 2) {
                std::cout << "c [f5] degree " << d << " mutants " << mutants.size() << " rows " << mdescs.size()
                          << " rank " << rank << " (was " << erows << ") T: " << std::fixed
                          << std::setprecision(2) << cpuTime() << std::endl;
            }
            const bool grew = (size_t)rank > erows;
            erows = rank;
            if (!grew) break;
        }
        // the rows of this degree supersede the basis: they contain every
        // earlier element multiplied up (and reduced), so take them as the
        // current basis
        basis = degree_rows;
        prev_leads.clear();
        for (const Poly& p : basis) prev_leads.insert(p[0]);
        if (st.budget_exhausted) break;
        if (finished()) break;
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
