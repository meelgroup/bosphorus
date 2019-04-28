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

#ifndef GAUSSJORDAN_H__
#define GAUSSJORDAN_H__

#include <limits>
#include <unordered_map>

#include "anf.hpp"
#include "m4ri.h"
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

        // Process Gauss Jordan output results
        for (int row = 0; row < mat->nrows; row++) {
            // Read row
            BoolePolynomial poly(mzd_read_bit(mat, row, mat->ncols - 1), ring);
            for (int col = 0; col < mat->ncols - 1; col++) {
                if (mzd_read_bit(mat, row, col)) {
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

        // Sort in descending degree-lex order
        std::sort(revMonomMap.begin(), revMonomMap.end(),
                  [](const BooleMonomial& rhs, const BooleMonomial& lhs) {
                      if (lhs.deg() == rhs.deg()) {
                          vector<uint32_t> lhs_v, rhs_v;
                          for (uint32_t v : lhs) {
                              lhs_v.push_back(v);
                          }
                          for (uint32_t v : rhs) {
                              rhs_v.push_back(v);
                          }
                          assert(lhs_v.size() == rhs_v.size());
                          std::sort(lhs_v.begin(), lhs_v.end());
                          std::sort(rhs_v.begin(), rhs_v.end());
                          for (size_t i = 0; i < lhs_v.size(); i++) {
                              if (lhs_v[i] == rhs_v[i]) {
                                  continue;
                              } else {
                                  return lhs_v[i] < rhs_v[i];
                              }
                          }
                          return false;
                      } else {
                          return lhs.deg() < rhs.deg();
                      }
                  });

        // assign numbering
        for (size_t i = 0; i < revMonomMap.size(); ++i)
            monomMap[revMonomMap[i].hash()] = i;
    }
};

}

#endif //__GAUSSJORDAN_H__
