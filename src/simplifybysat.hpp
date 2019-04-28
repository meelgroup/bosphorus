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

#ifndef SIMPLIFYBYSAT_H
#define SIMPLIFYBYSAT_H

#include <fstream>
#include "anf.hpp"
#include "cnf.hpp"
#include "solution.h"

namespace CMSat {
class SATSolver;
}

namespace BLib {

class SimplifyBySat
{
   public:
    SimplifyBySat(const CNF& cnf, const ConfigData& _config);
    ~SimplifyBySat();

    int simplify(const uint64_t numConfl_lim, const uint64_t numConfl_inc,
                 const double time_limit, const size_t cbeg,
                 vector<BoolePolynomial>& loop_learnt, ANF& anf,
                 Solution& solution);

   private:
    const ConfigData& config;
    const CNF& cnf;
    CMSat::SATSolver* solver;

    void addClausesToSolver(size_t beg);
    int extractUnitaries(vector<BoolePolynomial>& loop_learnt);
    int extractBinaries(vector<BoolePolynomial>& loop_learnt);
    int extractLinear(vector<BoolePolynomial>& loop_learnt);
    bool addPolynomial(vector<BoolePolynomial>& loop_learnt,
                       const pair<vector<uint32_t>, bool>& cnf_poly);
    int process(vector<BoolePolynomial>& loop_learnt,
                const vector<pair<vector<uint32_t>, bool> >& extracted);
};

}

#endif //SIMPLIFYBYSAT_H
