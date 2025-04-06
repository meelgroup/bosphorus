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

#pragma once

#include <vector>
#include <polybori/polybori.h>

namespace BLib {

std::pair<bool, double> if_sample_and_clone(
    const std::vector<polybori::BoolePolynomial>& eqs, double log2size);

double sample_and_clone(const uint32_t verbosity,
                        const std::vector<polybori::BoolePolynomial>& eqs,
                        std::vector<polybori::BoolePolynomial>& equations,
                        double log2size);

double do_sample_and_clone(const uint32_t verbosity,
                           const std::vector<polybori::BoolePolynomial>& eqs,
                           std::vector<polybori::BoolePolynomial>& equations,
                           double log2size);

void substitute(const polybori::BooleVariable& from_var,
               const polybori::BoolePolynomial& to_poly,
               polybori::BoolePolynomial& poly);

}
