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

#include "linfactor.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>

USING_NAMESPACE_PBORI
using std::vector;
using namespace BLib;

// In a product of linear factors with disjoint variables, two variables
// occur together in some monomial exactly when they are in different
// factors (a monomial takes at most one variable from each factor, and any
// two variables from different factors do appear together). So the factors
// are the classes of variables with identical co-occurrence sets. The
// constant of a factor is read off a monomial made of one variable from
// every other factor, and the guess is verified by re-expanding it.
bool BLib::factor_into_linerals(const BoolePolynomial& poly,
                                vector<Lineral>& factors)
{
    factors.clear();
    if (poly.isConstant()) return false;
    const BoolePolyRing& ring = poly.ring();

    vector<uint32_t> vars;
    for (const uint32_t v : poly.usedVariables()) vars.push_back(v);
    const size_t n = vars.size();
    std::unordered_map<uint32_t, uint32_t> idx;
    for (size_t i = 0; i < n; i++) idx[vars[i]] = i;

    if (poly.deg() == 1) {
        Lineral l;
        l.vars = vars;
        l.c = poly.hasConstantPart();
        factors.push_back(l);
        return true;
    }

    // co-occurrence matrix
    vector<vector<char> > adj(n, vector<char>(n, 0));
    vector<uint32_t> mvars;
    for (const BooleMonomial& m : poly) {
        if (m.deg() < 2) continue;
        mvars.clear();
        for (const uint32_t v : m) mvars.push_back(idx[v]);
        for (size_t a = 0; a < mvars.size(); a++) {
            for (size_t b = a + 1; b < mvars.size(); b++) {
                adj[mvars[a]][mvars[b]] = 1;
                adj[mvars[b]][mvars[a]] = 1;
            }
        }
    }

    // classes of identical co-occurrence rows, ordered by their first variable
    std::map<vector<char>, vector<uint32_t> > classes;
    for (size_t i = 0; i < n; i++) {
        bool any = false;
        for (size_t j = 0; j < n; j++) any |= adj[i][j];
        if (!any) return false; // a variable that meets no other cannot be in a product
        classes[adj[i]].push_back(vars[i]);
    }
    if (classes.size() < 2) return false;
    vector<vector<uint32_t> > parts;
    for (auto& kv : classes) parts.push_back(kv.second);
    std::sort(parts.begin(), parts.end(),
              [](const vector<uint32_t>& a, const vector<uint32_t>& b) {
                  return a.front() < b.front();
              });

    // constants and the candidate product
    const BooleSet pset = poly.set();
    BoolePolynomial product(true, ring);
    for (size_t i = 0; i < parts.size(); i++) {
        BooleMonomial probe(ring);
        for (size_t j = 0; j < parts.size(); j++) {
            if (j != i) probe *= ring.variable(parts[j].front());
        }
        Lineral l;
        l.vars = parts[i];
        l.c = pset.owns(probe);
        BoolePolynomial f(l.c, ring);
        for (const uint32_t v : l.vars) f += ring.variable(v);
        product *= f;
        factors.push_back(l);
    }
    if (product != poly) {
        factors.clear();
        return false;
    }
    return true;
}
