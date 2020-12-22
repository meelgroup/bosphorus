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

#include "extendedlinearization.hpp"
#include "anfutils.hpp"
#include "gaussjordan.hpp"

using std::vector;

using namespace BLib;

bool BLib::extendedLinearization(const ConfigData& config,
                           const vector<BoolePolynomial>& eqs,
                           vector<BoolePolynomial>& loop_learnt)
{
    if (eqs.empty()) {
        if (config.verbosity >= 3) {
            cout << "c System is empty. Skip XL\n";
        }
        return true;
    }

    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [XL] Running XL... ring size: "
             << eqs.front().ring().nVariables() << endl;
    }

    const size_t loop_learnt_size_orig = loop_learnt.size();
    const polybori::BoolePolyRing& ring(eqs.front().ring());

    vector<BoolePolynomial> equations;
    map<uint32_t, vector<BoolePolynomial> > deg_buckets;
    unordered_set<uint32_t> unique_poly_degrees;
    vector<uint32_t> sorted_poly_degrees;

    // Get a copy;
    const size_t XLsample =
        config.XLsample + config.XLsampleX; // amount of expansion allowed
    double numUnique =
        sample_and_clone(config.verbosity, eqs, equations,
                         config.XLsample); // give some leeway for expansion

    // Put them into degree buckets
    for (const BoolePolynomial& poly : equations) {
        uint32_t poly_deg = poly.deg();
        unique_poly_degrees.insert(poly_deg);
        deg_buckets[poly_deg].push_back(poly);
    }
    sorted_poly_degrees.assign(unique_poly_degrees.begin(),
                               unique_poly_degrees.end());
    sort(sorted_poly_degrees.begin(), sorted_poly_degrees.end());

    // Expansion step
    bool done_expansion = false;
    unsigned long nVars = ring.nVariables();
    for (uint32_t deg = 1; deg <= config.xlDeg && !done_expansion; deg++) {
        for (uint32_t poly_deg : sorted_poly_degrees) {
            const vector<BoolePolynomial>& to_expand = deg_buckets[poly_deg];
            if (config.verbosity >= 3) {
                cout << "c  There are " << to_expand.size()
                     << " polynomials of degree " << poly_deg << endl;
            }
            for (const BoolePolynomial& poly : to_expand) {
                const double log2eqsz = log2(equations.size());
                if (log2eqsz + numUnique > XLsample) {
                    done_expansion = true;
                    break;
                } else {
                    // Ugly hack
                    // To do: Efficient implementation of "nVars choose xlDeg"
                    //        e.g. http://howardhinnant.github.io/combinations.html?
                    if (deg >= 1) {
                        //how many variables to be multiplied with
                        const size_t num_variables_allowed = std::min(
                            nVars,
                            static_cast<size_t>(ceil(exp(
                                log(2) * (XLsample - log2eqsz - numUnique)))));

                        if (config.verbosity >= 3 &&
                            num_variables_allowed < nVars
                        ) {
                            cout << "c  Allowing " << num_variables_allowed
                                 << " variables in degree one expansion\n";
                        }

                        if (num_variables_allowed == nVars) {
                            for (unsigned long i = 0; i < nVars; ++i) {
                                BooleVariable v = ring.variable(i);
                                equations.push_back(BoolePolynomial(v * poly));
                            }
                        } else {
                            if (config.verbosity >= 4) {
                                cout << "c   Adding variables [";
                            }

                            unsigned long i = 0;
                            for (size_t n = 0; n < num_variables_allowed; ++n) {
                                double p = (double)(num_variables_allowed - n) / (double)(nVars - i);
                                if (p == 0) {
                                    p = 0.01;
                                }
                                i += std::floor(
                                    std::log(((double)rand()/(double)RAND_MAX)) / std::log(1.0 - p)
                                );

                                if (i >= nVars) {
                                    i = nVars - 1;
                                }

                                if (config.verbosity >= 4) {
                                    cout << i << ' ';
                                }

                                BooleVariable v = ring.variable(i);
                                equations.push_back(BoolePolynomial(v * poly));
                            }
                            if (config.verbosity >= 4)
                                cout << "] in degree one expansion\n";
                        }
                        numUnique += log2(num_variables_allowed);
                    }

                    //When degree 2 expansion is allowed
                    if (deg >= 2) {
                        for (unsigned long i = 0; i < nVars; ++i) {
                            for (unsigned long j = i + 1; j < nVars; ++j) {
                                BooleVariable v1 = ring.variable(i);
                                BooleVariable v2 = ring.variable(j);
                                equations.push_back(
                                    BoolePolynomial(v1 * v2 * poly));
                            }
                        }
                        numUnique += 2 * log2(nVars) - 1;
                    }
                    if (deg >= 3) {
                        for (unsigned long i = 0; i < nVars; ++i) {
                            for (unsigned long j = i + 1; j < nVars; ++j) {
                                for (unsigned long k = j + 1; k < nVars; ++k) {
                                    BooleVariable v1 = ring.variable(i);
                                    BooleVariable v2 = ring.variable(j);
                                    BooleVariable v3 = ring.variable(k);
                                    equations.push_back(
                                        BoolePolynomial(v1 * v2 * v3 * poly));
                                }
                            }
                        }
                        numUnique += 3 * log2(nVars) - 2;
                    }
                }
            } //for poly
            if (done_expansion) {
                break;
            }
        } //for poly_deg
        if (done_expansion) {
            break;
        }
    } // for deg
    // Run GJE after expansion

    GaussJordan gj(equations, ring, config.verbosity);
    long num = gj.run(NULL, &loop_learnt);

    if (config.verbosity) {
        cout << "c [XL] Done. Learnt: "
             << (loop_learnt.size() - loop_learnt_size_orig)
             << " T: " << std::fixed << std::setprecision(2)
             << (cpuTime() - myTime) << endl;
    }

    return num != GaussJordan::BAD;
}
