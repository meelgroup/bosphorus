/*****************************************************************************
Copyright (C) 2019  Kian Ming A. Chai, DSO National Laboratories

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

#include "anfcnfutils.hpp"

USING_NAMESPACE_PBORI
using namespace Bosph;
using namespace std;
typedef polybori::CCuddNavigator::value_type vidx_t;

/*******************************************************************************
Based on Michael Brickenstein (2009), Boolean Gröbner Bases: Theory, 
Algorithms and Applications
Communicated by Michael Brickenstein via Mate Soos  

Instead of computing the zeros then taking the complement (as suggested by the
original work), we compute the ones directly on the ZDDs 

Remark: Apparently, something similar happens in
https://github.com/sagemath/sage/blob/master/src/sage/sat/converters/polybori.py
********************************************************************************/

template <typename Iter>
static BooleSet powerset_r(const BoolePolyRing& ring, const Iter i,
                           const Iter end)
{
    if (i == end)
        return ring.one();
    else {
        Iter ii(i);
        ++ii;
        BooleSet S = powerset_r(ring, ii, end);
        return BooleSet(*i, S, S);
    }
}

/* Reasoning on the ZDD, based on Algo22 in the thesis */
static BooleSet BrickensteinAlgo22b(const BoolePolyRing& ring,
                                    BoolePolynomial::navigator np,
                                    const BooleSet::navigator S)
{
    if (np.isTerminated())
        return BooleSet(ring, S);

    if (np.isEmpty() || S.isEmpty()) // nothing (left to) satisfy(ies)
        return BooleSet(ring);

    const vidx_t v = *S;
    const BooleSet::navigator slice = S.elseBranch(); // get the quotient set
    if (*np > v) {
        // p0 == np and p1 = 0
        BooleSet Z = BrickensteinAlgo22b(ring, np, slice);
        return BooleSet(v, Z, Z);
    } else { // *np == v
        BoolePolynomial::navigator np0 = np.elseBranch();
        BoolePolynomial::navigator np1 = np.thenBranch();
        BooleSet Z0 = BrickensteinAlgo22b(ring, np0, slice);
        if (np0 == np1) {
            return BooleSet(v, BooleSet(ring), Z0);
        } else {
            BooleSet Z1 = BrickensteinAlgo22b(ring, np1, slice);
            return BooleSet(v, Z1.Xor(Z0), Z0);
        }
    }
}

static inline BooleSet BrickensteinAlgo23b(const BoolePolynomial& poly,
                                           const vector<vidx_t>& vidx)
{
    // We reason with ones directly, rather then with the complement of zeros.
    return BrickensteinAlgo22b(
        poly.ring(), poly.navigation(),
        powerset_r(poly.ring(), vidx.begin(), vidx.end()).navigation());
}

bool BrickesteinAlgo32(const BoolePolynomial& poly,
                       vector<Clause>& setofClauses)
{
    const size_t n = poly.nUsedVariables();
    vector<vidx_t> vidx(n);
    std::copy_n(poly.usedVariables().begin(), n, vidx.begin());
    BooleSet O = BrickensteinAlgo23b(poly, vidx);
    BooleSet T = O;
    while (T.size() > 0) {
        BooleMonomial o = T.lastLexicographicalTerm();
        BooleSet H = o.set();
        vector<Lit> c;
        for (size_t j = 0; j < n; ++j) {
            BooleSet dH = H.change(vidx[j]);
            if (O.contains(dH)) {
                H = H.Xor(dH);
            } else {
                if (o.reducibleBy(poly.ring().variable(vidx[j])))
                    c.push_back(Lit(vidx[j], true));
                else
                    c.push_back(Lit(vidx[j], false));
            }
        }
        if (!c.empty()) { // this two lines are indented wrongly in the thesis
            setofClauses.push_back(c);
        }
        T = T.diff(H);
    }
    return true;
}
