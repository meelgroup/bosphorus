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

#pragma once

#include <cstdint>
#include <vector>
#include <polybori/polybori.h>

namespace BLib {

/// A linear factor x_1 + ... + x_k + c ("lineral").
struct Lineral {
    std::vector<uint32_t> vars; // sorted
    bool c = false;
};

/// Tries to write `poly` as a product of linear factors with pairwise
/// disjoint variables, i.e. as an XNF clause: a polynomial
/// (l_1 + c_1) * ... * (l_k + c_k) = 0 says that some lineral l_i equals c_i.
/// Returns false if `poly` is not of that form. A polynomial of degree 1 is
/// its own single factor.
bool factor_into_linerals(const polybori::BoolePolynomial& poly,
                          std::vector<Lineral>& factors);

}
