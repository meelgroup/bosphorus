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
#include <vector>
#include "boolf4.hpp"

namespace BLib {

// Matrix-F5 (Bardet, Faugere, Salvy) over the Boolean ring in up to 64
// variables. Degree by degree, the rows m*f_i (m squarefree of degree
// d - deg f_i) are added in the order of the generators; the F5 criterion
// drops m*f_i when m is a leading monomial of the degree-(d - deg f_i)
// matrix of f_1..f_{i-1}, which for regular sequences removes every row
// that would reduce to zero. Rows are reduced against the reduced echelon
// form of the rows of lower index with M4RI, in groups of generators.
class BoolF5 {
   public:
    typedef BoolF4::Mon Mon;
    typedef BoolF4::Poly Poly;

    struct Options {
        uint32_t maxDeg = 64;
        uint64_t maxCells = 12000000000ULL;
        uint32_t groups = 8;   // generator groups per degree (more: better pruning, more eliminations)
        int verbosity = 0;
    };
    struct Stats {
        uint64_t rows = 0, rows_pruned = 0, cols_max = 0, zero_rows = 0;
        uint32_t max_deg = 0;
        bool budget_exhausted = false;
    };

    BoolF5(uint32_t nvars, const Options& opts) : n(nvars), opt(opts) {}
    void add(const Poly& p);
    // returns a reduced Groebner basis (or the partial basis at the budget)
    std::vector<Poly> run();
    const Stats& stats() const { return st; }

   private:
    uint32_t n;
    Options opt;
    Stats st;
    std::vector<Poly> F;      // generators, sorted by degree
    std::vector<int> Fdeg;
};

}
