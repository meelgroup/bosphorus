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
    bool operator<(const Lineral& o) const
    {
        return vars != o.vars ? vars < o.vars : c < o.c;
    }
    bool operator==(const Lineral& o) const { return vars == o.vars && c == o.c; }
};

typedef std::vector<uint32_t> VarVec; // a monomial as its sorted variable indices
struct VarVecHash {
    size_t operator()(const VarVec& v) const
    {
        size_t h = 1469598103934665603ULL;
        for (const uint32_t x : v) h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

/// Same as factor_into_linerals but on a list of monomials (the empty
/// vector is the constant 1), without building any ZDD. Verified by
/// expanding the factors as lists.
bool factor_terms(const std::vector<VarVec>& terms, std::vector<Lineral>& factors);

/// Normalises a factor list: sorts it, merges identical factors (l*l = l)
/// and detects a pair l, l+1 (the product is 0). Returns false when the
/// product is identically 0, i.e. the equation is trivially true.
bool normalize_product(std::vector<Lineral>& factors);

/// Canonical key of a product: factors sorted, each followed by a
/// separator carrying its constant.
VarVec product_key(const std::vector<Lineral>& factors);

/// Product of the factor sizes (|l_i| + c_i): the number of terms of the
/// expansion when the factors share no variable, an upper bound otherwise.
size_t product_size(const std::vector<Lineral>& factors);

/// Tries to write `poly` as a product of linear factors with pairwise
/// disjoint variables: a polynomial
/// (l_1 + c_1) * ... * (l_k + c_k) = 0 says that some lineral l_i equals c_i.
/// Returns false if `poly` is not of that form. A polynomial of degree 1 is
/// its own single factor.
bool factor_into_linerals(const polybori::BoolePolynomial& poly,
                          std::vector<Lineral>& factors);

/// The polynomial (l_1 + c_1) * ... * (l_k + c_k).
polybori::BoolePolynomial expand_linerals(const polybori::BoolePolyRing& ring,
                                          const std::vector<Lineral>& factors);

/// Substitutes variable `v` by `w + c` (or by the constant `c` when
/// `w == v`... see `subst_const`) in every factor. Variables may thereby
/// become shared between factors, which is fine for the clause encoding.
/// A factor that becomes the constant 1 is dropped; if one becomes the
/// constant 0 the product is 0 and `factors` is cleared and false returned.
bool subst_lineral_var(std::vector<Lineral>& factors, uint32_t v, uint32_t w, bool c);
bool subst_lineral_const(std::vector<Lineral>& factors, uint32_t v, bool c);

}
