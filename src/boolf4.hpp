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
#include <algorithm>

namespace BLib {

// Open-addressing hash set of monomial masks (linear probing, all-ones as
// the empty slot: a monomial in all 64 variables never occurs, the
// engines take at most 63). std::unordered_set allocated a node per
// insert and was a third of the symbolic preprocessing.
class MonSet {
   public:
    typedef uint64_t Mon;
    static constexpr Mon EMPTY = ~(Mon)0;
    MonSet() { rehash(1024); }
    void clear() { std::fill(tab.begin(), tab.end(), EMPTY); vals.assign(vals.size(), 0); n = 0; }
    size_t size() const { return n; }
    bool count(Mon k) const { return find(k) != (size_t)-1; }
    // inserts k; returns true if it was not there
    bool insert(Mon k, uint32_t v = 0)
    {
        if (2 * (n + 1) > tab.size()) rehash(tab.size() * 2);
        size_t i = slot(k);
        while (tab[i] != EMPTY) {
            if (tab[i] == k) return false;
            i = (i + 1) & mask;
        }
        tab[i] = k;
        if (!vals.empty()) vals[i] = v;
        n++;
        return true;
    }
    // value stored with k (insert with a value first); (uint32_t)-1 if absent
    uint32_t get(Mon k) const
    {
        const size_t i = find(k);
        return i == (size_t)-1 ? (uint32_t)-1 : vals[i];
    }
    void reserve(size_t cnt)
    {
        size_t cap = 1024;
        while (cap < 2 * cnt) cap *= 2;
        if (cap > tab.size()) rehash(cap);
    }
    void with_values(bool yes) { vals.assign(yes ? tab.size() : 0, 0); }

   private:
    std::vector<Mon> tab;
    std::vector<uint32_t> vals; // parallel to tab when used as a map
    size_t mask = 0, n = 0;
    size_t slot(Mon k) const { return (size_t)((k * 0x9E3779B97F4A7C15ULL) >> 20) & mask; }
    size_t find(Mon k) const
    {
        size_t i = slot(k);
        while (tab[i] != EMPTY) {
            if (tab[i] == k) return i;
            i = (i + 1) & mask;
        }
        return (size_t)-1;
    }
    void rehash(size_t cap)
    {
        std::vector<Mon> old;
        old.swap(tab);
        std::vector<uint32_t> oldv;
        oldv.swap(vals);
        tab.assign(cap, EMPTY);
        mask = cap - 1;
        n = 0;
        const bool has_vals = !oldv.empty();
        if (has_vals) vals.assign(cap, 0);
        for (size_t i = 0; i < old.size(); i++) {
            if (old[i] != EMPTY) insert(old[i], has_vals ? oldv[i] : 0);
        }
    }
};

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
        uint64_t maxCells = 12000000000ULL; // rows*cols of any single matrix (1.5 GB of bits)
        int verbosity = 0;
        bool tailReduce = true;      // interreduce the basis tails after every step
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
    std::vector<Poly> F0; // the generators, for the solved check
    std::vector<Mon> columns_scratch;

    void add_to_basis(const Poly& p);
    void update_pairs(int h);
    void reduce_step(std::vector<Pair>& selected);
    std::vector<Poly> echelon(std::vector<Poly>& rows, std::vector<Mon>& columns);
    void interreduce();
    void tail_reduce();
    bool solved();
};

}
