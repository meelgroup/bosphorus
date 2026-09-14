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
#include "boolf4.hpp"

namespace BLib {

// Groebner bases by case splitting (rule gb-split).
//
// The degree a basis computation has to reach is what makes it expensive:
// for a random quadratic system with m = 2n equations over GF(2) the
// degree of regularity is 4 up to n = 27 and 5 from n = 28, and the
// degree-5 matrix has five times the columns of the degree-4 one (122k
// against 24k for n = 28). Fixing one variable brings the system back to
// degree 4, so two bases at degree 4 cost a small fraction of one at
// degree 5.
//
// run() computes the basis with the matrix engine (F4 or F5) under the
// cell and degree budgets; when a step exceeds them the system is split on
// a variable x and the two specialised systems x = 0 and x = 1 are done
// recursively. The branches combine soundly:
//   - a branch whose basis is {1} is inconsistent: x is forced to the
//     other value and every member of the other branch's basis holds;
//   - otherwise members f0 (from x = 0) and f1 (from x = 1) with the same
//     leading monomial give f0 + x*(f0 + f1), which is f0 when x = 0 and
//     f1 when x = 1, so it lies in the ideal; f0 == f1 gives f0 itself.
//     A member without a partner gives the conditional (x + 1)*f0 or x*f1.
// Every polynomial returned is in the ideal of the input together with
// the field equations; the set is a Groebner basis only when no split
// happened. All budgets are deterministic counts.
class BoolSplit {
   public:
    typedef BoolF4::Mon Mon;
    typedef BoolF4::Poly Poly;

    struct Options {
        uint32_t maxDepth = 0;      // 0: never split (plain engine run)
        uint32_t maxDeg = 64;       // degree cap of every engine run
        uint64_t maxCells = 4000000000ULL; // rows*cols of any single matrix
        uint64_t maxRows = 20000000; // matrix rows over the whole recursion
        int engine = 1;             // 1 = F4, 2 = F5
        uint32_t f5groups = 8;
        uint32_t condLen = 8;       // conditional facts (x+1)*f longer than this are dropped
        int verbosity = 0;
    };

    struct Stats {
        uint64_t branches = 0;        // engine runs (the root included)
        uint64_t skipped = 0;         // nodes split without an engine run (the sibling had to split)
        uint64_t unsat_branches = 0;  // runs whose basis was {1}
        uint64_t solved_branches = 0; // runs whose basis fixed every variable
        uint64_t budget_branches = 0; // runs that hit a budget and could not split further
        uint64_t rows = 0;            // matrix rows over all runs
        uint32_t max_depth = 0;       // deepest split
        bool complete = true;         // no run ended on a budget
    };

    BoolSplit(uint32_t nvars, const Options& opts) : n(nvars), opt(opts) {}

    // generators over the variables 0..n-1 (monomials in any order)
    void add(const Poly& p);

    // polynomials of the ideal; {0} (a single zero monomial) if it is the
    // whole ring
    std::vector<Poly> run();

    const Stats& stats() const { return st; }

   private:
    struct Result {
        std::vector<Poly> polys;
        bool has_one = false;
        bool complete = true;
        bool split = false; // this node had to split
    };

    uint32_t n;
    Options opt;
    Stats st;
    std::vector<Poly> gens;

    // try_engine = false: the sibling branch at this depth had to split,
    // so the engine run is skipped and the system split right away (its
    // sibling's matrix would have been the same size)
    Result solve(const std::vector<Poly>& g, uint32_t nv, uint32_t depth, bool try_engine);
    Result engine_run(const std::vector<Poly>& g, uint32_t nv, bool& exhausted);
    static Result combine(const Result& r0, const Result& r1, uint32_t x, uint32_t condLen);
};

}
