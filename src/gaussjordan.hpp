/*****************************************************************************
Copyright (C) 2016  Security Research Labs
Copyright (C) 2018  Mate Soos, Davin Choo, Kian Ming A. Chai, DSO National Laboratories

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

#pragma once

#include <limits>
#include <unordered_map>

#include "anf.hpp"
#include <m4ri/m4ri.h>
#include "time_mem.h"

using std::cout;
using std::endl;
using std::make_pair;
using std::unordered_map;
using std::vector;

namespace BLib {

class GaussJordan
{
   public:
    GaussJordan(const vector<BoolePolynomial>& equations,
                const BoolePolyRing& ring, uint32_t _verbosity)
        : verbosity(_verbosity), ring(ring)
    {
        // Initialize mapping
        buildMaps(equations);

        // Initialize matrix
        // number of rows = equations.size()
        // number of cols = nextVar + 1
        mat = mzd_init(equations.size(), revMonomMap.size() + 1);
        assert(mzd_is_zero(mat));
        if (verbosity >= 4) {
            cout << "c   Matrix size: " << mat->nrows << " x " << mat->ncols
                 << endl;
        }

        // Add equations to matrix
        uint32_t row = 0;
        for (const BoolePolynomial& poly : equations) {
            for (const BooleMonomial& mono : poly) {
                // Process non-empty monomials (aka non-constant)
                if (mono.deg() != 0) {
                    auto it = monomMap.find(mono.hash());
                    assert(it != monomMap.end());
                    uint32_t col = it->second;
                    mzd_write_bit(mat, row, col, 1);
                }
            }
            // Constant goes here
            mzd_write_bit(mat, row, mat->ncols - 1, poly.hasConstantPart());
            row++;
        }
    }

    ~GaussJordan()
    {
        mzd_free(mat);
    }

    const mzd_t* getMatrix() const
    {
        return mat;
    }

    void printMatrix() const
    {
        for (int r = 0; r < mat->nrows; r++) {
            for (int c = 0; c < mat->ncols; c++) {
                cout << mzd_read_bit(mat, r, c) << " ";
            }
            cout << endl;
        }
        cout << endl;
    }

    long run(vector<BoolePolynomial>* all_equations,
             vector<BoolePolynomial>* learnt_equations)
    {
        double startTime = cpuTime();
        long num_linear = 0;
        if (all_equations != NULL) {
            all_equations->clear();
        }

        // Matrix includes augmented column
        assert(mat->ncols > 0);

        // See: https://malb.bitbucket.io/m4ri/echelonform_8h.html
        if (verbosity >= 6) {
            cout << "c Before Gauss Jordan\n";
            printMatrix();
        }
        mzd_echelonize_m4ri(mat, true, 0);
        if (verbosity >= 6) {
            cout << "c After Gauss Jordan\n";
            printMatrix();
        }

        // Process Gauss Jordan output results: scan the words of each row for
        // set bits instead of reading every cell (the matrix is sparse and
        // wide)
        const int const_col = mat->ncols - 1;
        for (int row = 0; row < mat->nrows; row++) {
            BoolePolynomial poly(mzd_read_bit(mat, row, const_col), ring);
            const word* r = mzd_row(mat, row);
            for (int w = 0; w < mat->width; w++) {
                word x = r[w];
                while (x) {
                    const int col = w * 64 + __builtin_ctzll(x);
                    x &= x - 1;
                    if (col >= const_col) break;
                    poly += revMonomMap[col];
                }
            }
            if (poly.isZero())
                continue;
            else if (poly.isOne()) {
                if (verbosity >= 1) {
                    cout << "c [GJ] UnSAT\n";
                }
                if (learnt_equations != NULL) {
                    learnt_equations->push_back(BoolePolynomial(1, ring));
                }
                return BAD;
            }

            // Process polynomial
            if (poly.deg() == 1) {
                num_linear++;
            }
            if (all_equations != NULL) {
                all_equations->push_back(poly);
            }
            if (learnt_equations != NULL) {
                if (poly.deg() == 1) {
                    // linear equation
                    learnt_equations->push_back(poly);
                } else if (poly.isPair() && poly.hasConstantPart()) {
                    // a*b*c*...*z + 1 = 0
                    learnt_equations->push_back(poly);
                }
            }
        }
        if (verbosity >= 4) {
            cout << "c   Gauss Jordan in " << (cpuTime() - startTime)
                 << " seconds." << endl;
        }
        return num_linear;
    }

    static const long BAD = std::numeric_limits<long>::min();

   private:
    uint32_t verbosity;
    mzd_t* mat;
    const BoolePolyRing& ring;
    std::unordered_map<BooleMonomial::hash_type, uint32_t> monomMap;
    std::vector<BooleMonomial> revMonomMap;

    void buildMaps(const vector<BoolePolynomial> equations)
    {
        // Gather all monomials
        for (const BoolePolynomial& poly : equations) {
            for (const BooleMonomial& mono : poly) {
                // Ignore constant monomial 1
                if (mono.deg() != 0) {
                    auto ins = monomMap.insert(make_pair(mono.hash(), 0));
                    if (ins.second)
                        revMonomMap.push_back(mono);
                }
            }
        }

        // Sort in descending degree-lex order. The keys (sorted variable
        // lists) are computed once per monomial: doing it inside the
        // comparator made this the hottest part of XL.
        vector<vector<uint32_t> > keys(revMonomMap.size());
        for (size_t i = 0; i < revMonomMap.size(); i++) {
            for (uint32_t v : revMonomMap[i]) keys[i].push_back(v);
        }
        vector<size_t> order(revMonomMap.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            const vector<uint32_t>& ka = keys[a];
            const vector<uint32_t>& kb = keys[b];
            if (ka.size() != kb.size()) return ka.size() > kb.size();
            for (size_t i = 0; i < ka.size(); i++) {
                if (ka[i] != kb[i]) return ka[i] > kb[i];
            }
            return false;
        });
        vector<BooleMonomial> sorted;
        sorted.reserve(revMonomMap.size());
        for (const size_t i : order) sorted.push_back(revMonomMap[i]);
        revMonomMap.swap(sorted);

        // assign numbering
        for (size_t i = 0; i < revMonomMap.size(); ++i)
            monomMap[revMonomMap[i].hash()] = i;
    }
};

}
