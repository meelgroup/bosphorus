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

#include "elimlin.hpp"
#include "anfutils.hpp"
#include "gaussjordan.hpp"
#include <iomanip>

using std::vector;

USING_NAMESPACE_PBORI
using namespace BLib;

// Implementation based on https://infoscience.epfl.ch/record/176270/files/ElimLin_full_version.pdf
bool BLib::elimLin(const ConfigData& config, const vector<BoolePolynomial>& eqs,
             vector<BoolePolynomial>& loop_learnt)
{
    //don't run if empty
    if (eqs.empty()) {
        return true;
    }

    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [ElimLin] Running ElimLin... ring size: "
             << eqs.front().ring().nVariables() << endl;
    }

    const size_t loop_learnt_size_orig = loop_learnt.size();
    const polybori::BoolePolyRing& ring(eqs.front().ring());
    vector<BoolePolynomial> all_equations;

    // Get a copy;
    sample_and_clone(config.verbosity, eqs, all_equations, config.ELsample);

    bool timeout = (cpuTime() > config.maxTime);
    bool fixedpoint = false;
    while (!fixedpoint && !timeout) {
        fixedpoint = true;

        // Perform Gauss Jordan
        GaussJordan gj(all_equations, ring, config.verbosity);
        const long num_linear = gj.run(&all_equations, NULL);
        if (num_linear == GaussJordan::BAD)
            return false;

        vector<std::pair<size_t, size_t> > linear_idx_nvar;
        for (size_t i = 0; i < all_equations.size(); i++)
            if (all_equations[i].deg() == 1)
                linear_idx_nvar.push_back(
                    std::make_pair(i, all_equations[i].nUsedVariables()));
        assert(num_linear == (long)linear_idx_nvar.size());

        //No linear equations, reached fixedpoint, exit
        if (num_linear == 0) {
            break;
        }

        if (config.verbosity >= 3) {
            cout << "c  Processing " << num_linear << " linear equations "
                 << "in a system of " << all_equations.size() << " equations"
                 << endl;
        }

        // preprocess the linear equations for efficient back-subsitution from the echleon form
        std::sort(linear_idx_nvar.begin(), linear_idx_nvar.end(),
                  [](const std::pair<size_t, size_t>& x,
                     const std::pair<size_t, size_t>& y) {
                      return x.second < y.second;
                  });

        // Create occurrence list for equations involved
        vector<unordered_set<size_t> > el_occ(ring.nVariables());
        for (size_t idx = 0; idx < all_equations.size(); idx++) {
            const BoolePolynomial& poly = all_equations[idx];
            for (const uint32_t v : poly.usedVariables()) {
                el_occ[v].insert(idx);
            }
        }

        // Iterate through all linear equations
        for (const auto& in : linear_idx_nvar) {
            const size_t linear_idx = in.first;
            const BoolePolynomial& linear_eq = all_equations[linear_idx];
            if (!linear_eq.isConstant()) {
                fixedpoint = false;
                loop_learnt.push_back(linear_eq);

                // Pick variable with best metric to substitute
                uint32_t var_to_replace = 0;
                size_t best_metric = std::numeric_limits<std::size_t>::max();
                assert(linear_eq.deg() == 1);
                for (const uint32_t v : linear_eq.usedVariables()) {
                    size_t metric = el_occ[v].size();
                    if (metric < best_metric) {
                        best_metric = metric;
                        var_to_replace = v;
                    }
                }

                BooleVariable from_var(var_to_replace, ring);
                BoolePolynomial to_poly(ring);
                to_poly = linear_eq - from_var;

                if (config.verbosity >= 5) {
                    cout << "c Replacing " << from_var << " with " << to_poly
                         << endl;
                }

                // Eliminate variable from these polynomials
                for (size_t idx : el_occ[var_to_replace]) {
                    BoolePolynomial& poly = all_equations[idx];
                    BooleMonomial prev_used = poly.usedVariables();

                    // Eliminate variable
                    if (linear_idx == idx)
                        poly = 0; // replacing itself
                    else {
                        substitute(from_var, to_poly, poly);
                        BooleMonomial curr_used(poly.usedVariables());
                        BooleMonomial gcd = prev_used.GCD(curr_used);
                        prev_used /= gcd; // update remove list
                        curr_used /= gcd; // update insert list
                        // Add back to el_occ
                        for (const uint32_t v : curr_used) {
                            auto ins = el_occ[v].insert(idx);
                            assert(ins.second);
                        }
                    } // if

                    // Remove from el_occ
                    for (const uint32_t v : prev_used) {
                        if (v != var_to_replace) {
                            size_t e = el_occ[v].erase(idx);
                            assert(e == 1);
                        }
                    }
                } // for idx
            }
        }
        timeout = (cpuTime() > config.maxTime);
    } // while(fixedpint && timeout)

    // Add possible useful knowledge back to actual ANF system
    for (BoolePolynomial& poly : all_equations) {
        // 1) Linear equations (includes assignments and anti/equivalences)
        // 2) abc...z + 1 = 0
        // 3) mono1 + mono2 = 0/1 [ Not done ]
        if (poly.deg() == 1 || (poly.isPair() && poly.hasConstantPart())) {
            loop_learnt.push_back(poly);
        }
    }

    if (config.verbosity) {
        cout << "c [ElimLin] Done. Learnt: "
             << (loop_learnt.size() - loop_learnt_size_orig)
             << " T: " << std::fixed << std::setprecision(2)
             << (cpuTime() - myTime) << endl;
    }

    return true;
}
