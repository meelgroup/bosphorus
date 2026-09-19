/*****************************************************************************
Copyright (C) 2018  Davin Choo, Kian Ming A. Chai, DSO National Laboratories

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

#include <iostream>

#include "anfutils.hpp"

using std::cout;
using std::endl;
using std::make_pair;
using std::pair;
using std::swap;
using std::vector;

USING_NAMESPACE_PBORI
using namespace BLib;

pair<bool, double> BLib::if_sample_and_clone(const vector<BoolePolynomial>& eqs,
                                       double log2size)
{
    const polybori::BoolePolyRing& ring(eqs.front().ring());
    const size_t log2fullsz =
        log2(eqs.size()) +
        2 * log2(ring.nVariables()); // assume quadratic equations only
    return make_pair(log2fullsz > log2size, log2fullsz);
}

double BLib::sample_and_clone(const uint32_t verbosity,
                        const vector<BoolePolynomial>& eqs,
                        vector<BoolePolynomial>& equations, double log2size)
{
    auto ret = if_sample_and_clone(eqs, log2size);
    if (!ret.first) {
        // Small system, so clone the entire system
        equations = eqs;
        return ret.second;
    } else {
        return do_sample_and_clone(verbosity, eqs, equations, log2size);
    }
}

double BLib::do_sample_and_clone(const uint32_t verbosity,
                           const vector<BoolePolynomial>& eqs,
                           vector<BoolePolynomial>& equations, double log2size)
{
    assert(equations.empty());
    // fill an indexing vector with identity
    vector<size_t> idx(eqs.size());
    for (size_t i = 0; i < eqs.size(); ++i)
        idx[i] = i;

    // randomly select equations until a limit
    BooleSet unique(eqs.front().ring());
    double log2uniquesz = 0;
    size_t sampled = 1, reject = 0;
    double rej_rate = 0;
    do {
        rej_rate = static_cast<double>(reject) / sampled;
        size_t sel =
            std::floor(static_cast<double>(rand()) / RAND_MAX * idx.size());
        const BoolePolynomial& poly(eqs[idx[sel]]);
        ++sampled;
        if (!unique.isZero() && rej_rate < 0.8) {
            // accept with probability of not increasing then number of monomials
            const size_t out =
                poly.length() - poly.set().intersect(unique).size();
            if (static_cast<double>(rand()) / RAND_MAX <
                static_cast<double>(out) / poly.length()) {
                ++reject;
                continue; // reject and continue with do-while loop
            }
        }
        equations.push_back(poly);
        swap(idx.back(), idx[sel]);
        idx.pop_back();
        unique = unique.unite(poly.set());
        log2uniquesz = log2(unique.size());
    } while ((log2(equations.size()) + log2uniquesz < log2size) &&
             (idx.size() > 0));
    if (verbosity >= 3)
        cout << "c  Selected " << equations.size() << '[' << unique.size()
             << "] equations with rejection rate " << rej_rate << endl;

    return log2uniquesz;
}

void BLib::substitute(const BooleVariable& from_var,
                      const BoolePolynomial& to_poly, BoolePolynomial& poly)
{
    const BooleSet s = poly.set();
    const BoolePolynomial p1(s.subset1(from_var.index()));
    if (p1.isZero()) return;
    poly = BoolePolynomial(s.subset0(from_var.index())) + to_poly * p1;
}
