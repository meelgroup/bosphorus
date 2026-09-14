/*****************************************************************************
Copyright (C) 2026  Mate Soos

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
***********************************************/

#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <polybori/polybori.h>

namespace BLib {

// An echelon basis of linear polynomials over GF(2) with the highest
// variable of every row as its pivot, kept fully reduced (Gauss-Jordan).
// Rows are bit vectors over the variables plus one constant bit.
class LinBasis {
   public:
    typedef std::vector<uint64_t> Row;

    explicit LinBasis(size_t num_vars) : words((num_vars + 63) / 64) {}

    Row row_of(const polybori::BoolePolynomial& p) const
    {
        Row r(words + 1, 0);
        for (const uint32_t v : p.usedVariables()) flip(r, v);
        r[words] = p.hasConstantPart();
        return r;
    }

    // number of variables in the row
    size_t length(const Row& r) const
    {
        size_t c = 0;
        for (size_t w = 0; w < words; w++) c += __builtin_popcountll(r[w]);
        return c;
    }

    bool constant(const Row& r) const { return r[words]; }

    // highest variable, -1 for a constant row
    long pivot(const Row& r) const
    {
        for (long w = words - 1; w >= 0; w--) {
            if (r[w]) return w * 64 + (63 - __builtin_clzll(r[w]));
        }
        return -1;
    }

    // Normal form modulo the basis. One pass from the highest bit down is
    // enough: adding a pivot row only changes bits below its pivot.
    void reduce(Row& r) const
    {
        for (long w = words - 1; w >= 0; w--) {
            uint64_t mask = ~(uint64_t)0;
            while (r[w] & mask) {
                const size_t b = 63 - __builtin_clzll(r[w] & mask);
                mask &= ((uint64_t)1 << b) - 1;
                const auto it = rows.find(w * 64 + b);
                if (it == rows.end()) continue;
                for (size_t k = 0; k <= words; k++) r[k] ^= it->second[k];
            }
        }
    }

    // Adds an already reduced, non-constant row; keeps the others reduced.
    void insert(const Row& r)
    {
        const long piv = pivot(r);
        for (auto& kv : rows) {
            if (bit(kv.second, piv)) for (size_t k = 0; k <= words; k++) kv.second[k] ^= r[k];
        }
        rows[piv] = r;
    }

    polybori::BoolePolynomial poly_of(const Row& r, const polybori::BoolePolyRing& ring) const
    {
        polybori::BoolePolynomial p(r[words], ring);
        for (size_t w = 0; w < words; w++) {
            uint64_t x = r[w];
            while (x) {
                p += polybori::BooleVariable(w * 64 + __builtin_ctzll(x), ring);
                x &= x - 1;
            }
        }
        return p;
    }

    size_t rank() const { return rows.size(); }

   private:
    static bool bit(const Row& r, size_t i) { return (r[i / 64] >> (i % 64)) & 1; }
    static void flip(Row& r, size_t i) { r[i / 64] ^= (uint64_t)1 << (i % 64); }

    const size_t words;
    std::unordered_map<uint32_t, Row> rows; // pivot -> row
};

}
