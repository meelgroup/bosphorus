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

#ifndef EVALUATOR_H__
#define EVALUATOR_H__

#include "solvertypesmini.h"
#include "polybori.h"

USING_NAMESPACE_PBORI

#include <map>

using std::map;
using std::vector;

namespace BLib {

inline const lbool evaluatePoly(const BoolePolynomial& poly,
                                const uint32_t setting,
                                const map<uint32_t, uint32_t>& mapping)
{
    // Doesn't need to be set to 1 in case there is a '+1'
    // In that case, there is an empty monomial, which will flip it
    bool ret = false;
    for (const BooleMonomial& mono : poly) {
        bool thisMonom = true;
        for (const uint32_t v : mono) {
            map<uint32_t, uint32_t>::const_iterator fit = mapping.find(v);
            assert(fit != mapping.end());
            thisMonom *= ((setting >> (fit->second)) & 1);
        }
        ret ^= thisMonom;
    }
    return boolToLBool(ret);
}

inline lbool evaluatePoly(const BoolePolynomial& poly, const vector<lbool>& sol)
{
    BoolePolynomial ret(poly.ring());
    for (const BooleMonomial& mono : poly) {
        BoolePolynomial thisSubPoly(true, poly.ring());
        for (const uint32_t v : mono) {
            assert((uint32_t)sol.size() > v);
            if (sol[v] == l_Undef) {
                thisSubPoly *= BooleVariable(v, poly.ring());
                continue;
            }
            thisSubPoly *= BooleConstant(sol[v] == l_True);
        }
        ret += thisSubPoly;
    }
    return boolToLBool(ret.isZero());
}

}

#endif //__EVALUATOR_H__
