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

namespace BLib {

// A matrix-F4 Groebner basis engine for the Boolean ring GF(2)[x]/(x^2+x)
// in up to 64 variables. Monomials are squarefree and stored as 64-bit
// masks, polynomials as lists of monomials sorted in decreasing
// degree-reverse-lexicographic order. Critical pairs of the lowest degree
// are reduced together in one dense GF(2) matrix (M4RI), as in Faugere's
// F4; the field equations enter through the Boolean product (x*m = m when
// x is in m) and through the "variable pairs" x*p for x in the leading
// monomial of p, which are what makes the result a Groebner basis of the
// ideal together with the field equations. Work is bounded by
// deterministic budgets (matrix rows, degree), never by time.
class BoolF4 {
   public:
    typedef uint64_t Mon;
    typedef std::vector<Mon> Poly;

    struct Options {
        uint32_t maxDeg = 64;        // pairs of higher degree are dropped (basis stays partial)
        uint64_t maxRows = 20000000; // total matrix rows over the run
        uint64_t maxCells = 4000000000ULL; // rows*cols of any single matrix
        int verbosity = 0;
    };

    struct Stats {
        uint64_t steps = 0, rows = 0, cols_max = 0, new_polys = 0, zero_reductions = 0;
        uint32_t max_deg = 0;
        bool budget_exhausted = false;
    };

    BoolF4(uint32_t nvars, const Options& opts);

    // the generators; monomials may be given in any order
    void add(const Poly& p);

    // runs the algorithm; returns the reduced basis (each polynomial with
    // its leading monomial first)
    std::vector<Poly> run();

    const Stats& stats() const { return st; }

    // degrevlex order on masks: a < b
    static bool less(Mon a, Mon b);
    static int deg(Mon m) { return __builtin_popcountll(m); }
    // canonical form: sorted decreasing, pairs of equal monomials cancelled
    static void normalize(Poly& p);
    // Boolean product of a polynomial with a monomial u
    static Poly mul(const Poly& p, Mon u);
    // p + q in GF(2)
    static Poly add(const Poly& p, const Poly& q);

   private:
    struct Pair {
        Mon lcm;
        int i, j; // j < 0: variable pair x_(-j-1) * G[i]
        int degree;
    };

    uint32_t n;
    Options opt;
    Stats st;
    std::vector<Poly> G;
    std::vector<Mon> LM;
    std::vector<char> alive; // basis elements superseded by others with dividing leads
    std::vector<Pair> pairs;
    bool has_one = false;

    void add_to_basis(const Poly& p);
    void update_pairs(int h);
    void reduce_step(std::vector<Pair>& selected);
    std::vector<Poly> echelon(std::vector<Poly>& rows, std::vector<Mon>& columns);
    void interreduce();
};

}
