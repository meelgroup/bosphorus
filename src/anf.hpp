/*****************************************************************************
Copyright (C) 2016  Security Research Labs
Copyright (C) 2018  Mate Soos, Davin Choo, Kian Ming A. Chai, DSO National Laboratories

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

#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

#include "anfstats.hpp"
#include "configdata.hpp"
#include "linfactor.hpp"
#include "evaluator.hpp"
#include "replacer.hpp"
#include <polybori/polybori.h>

USING_NAMESPACE_PBORI

using std::cout;
using std::endl;
using std::map;
using std::string;
using std::unordered_set;
using std::vector;

namespace BLib {

class Replacer;

struct anf_no_replacer_tag {
};

class ANF
{
   public:
    typedef unordered_set<BoolePolynomial::hash_type> eqs_hash_t;

   public:
    ANF(const polybori::BoolePolyRing* _ring, ConfigData& _config);
    ANF(const ANF& other, const anf_no_replacer_tag);
    ANF(const ANF&) = delete;
    ~ANF();

    size_t readFile(const string& filename);
    bool propagate();
    inline vector<lbool> extendSolution(const vector<lbool>& solution) const;
    void printStats() const;
    ANFStats get_stats() const;
    const ConfigData& get_config() const { return config; }
    void print_solution_map(std::ofstream* ofs);
    void get_solution_map(map<uint32_t, VarMap>& ret) const;

    // Returns true if polynomial is new and has been added
    bool addBoolePolynomial(const BoolePolynomial& poly);
    // Adds the polynomial given as a list of monomials (the empty vector is
    // the constant 1). If it is a product of linear factors it is stored as
    // such and never expanded into a ZDD unless a rule needs that.
    bool addTerms(vector<VarVec>& terms);
    bool addLearntBoolePolynomial(const BoolePolynomial& poly);
    void contextualize(vector<BoolePolynomial>& learnt) const;

    // others
    inline void setNOTOK(void);

    // Query functions
    size_t size() const;
    size_t deg() const;
    size_t getNumSimpleXors() const;
    inline size_t getNumReplacedVars() const;
    inline size_t getNumSetVars() const;
    inline size_t getNumVars() const;
    size_t numMonoms() const;
    //size_t numUniqueMonoms(const vector<BoolePolynomial>& equations) const;
    inline bool hasPolynomial(const BoolePolynomial& p) const;
    const BoolePolyRing& getRing() const;
    /// All equations as polynomials. Materialises every lazily stored
    /// product first: prefer eq(i) / isProduct(i) / getFactors(i).
    const vector<BoolePolynomial>& getEqs() const;
    /// Equation idx as a polynomial (expanded on demand for products).
    const BoolePolynomial& eq(size_t idx) const;
    /// Equation idx is stored as a product of linear factors.
    bool isProduct(size_t idx) const { return !factors[idx].empty(); }
    /// Variables of equation idx, without expanding a product.
    BooleMonomial varsOf(size_t idx) const;
    /// Calls f(var) for every variable of equation idx (no ZDD work for a
    /// product; a variable shared by two factors is visited twice).
    template <class F> void forEachVar(size_t idx, F f) const
    {
        if (poly_valid[idx]) {
            for (const uint32_t v : eqs[idx].usedVariables()) f(v);
        } else {
            for (const Lineral& l : factors[idx]) for (const uint32_t v : l.vars) f(v);
        }
    }
    /// Number of variables of equation idx (an upper bound for a product).
    size_t nVarsOf(size_t idx) const;
    /// Degree of equation idx (the number of factors for a product, an
    /// upper bound if the factors share variables).
    int degOf(size_t idx) const;
    /// The lineral factorisation of equation idx if known (a product of
    /// >= 2 linear factors), else empty. Found once with
    /// factor_into_linerals() and then maintained through propagation, so
    /// it stays known even when substitutions make the factors share
    /// variables and the expanded polynomial can no longer be factored.
    const vector<Lineral>& getFactors(size_t idx) const { return factors[idx]; }
    size_t getEqLen(size_t idx) const { return eq_len[idx]; }
    inline const vector<lbool>& getFixedValues() const;
    inline const eqs_hash_t& getEqsHash(void) const;
    const vector<vector<size_t> >& getOccur() const;
    inline bool getOK() const;
    bool evaluate(const vector<lbool>& vals) const;
    void checkOccur() const;
    inline lbool value(const uint32_t var) const;
    inline Lit getReplaced(const uint32_t var) const;
    inline ANF& operator=(const ANF& other);
    static size_t readFileForMaxVar(const std::string& filename);
    set<size_t> get_proj_set() const;

    // In-place rewrite rules (anfrewrite.cpp). Each returns the number of
    // changes it made; check getOK() afterwards, they may find UNSAT.
    size_t rewrite_inplace();        // all of the below, to a fixpoint
    size_t reduce_by_short_polys();  // "binom-red"
    size_t shorten_polys();          // "poly-shorten"
    size_t probe_small_polys();      // "lit-probe" (+ "impl-scc")
    size_t canon_factors();          // "fac-canon"
    size_t resolve_factors();        // "fac-res"
    size_t groebner_windows();       // "gb-window" (anfgroebner.cpp)
    /// Adds a product of >= 2 linear factors as a new equation; false if present
    bool addProduct(const vector<Lineral>& f);

   private:
    bool propagate_iteratively(unordered_set<uint32_t>& updatedVars,
                               std::vector<size_t>& empty_equations);
    bool check_if_need_update(const BoolePolynomial& poly,
                              unordered_set<uint32_t>& updatedVars);
    void addPolyToOccur(const BooleMonomial& mono, size_t eq_idx);
    void removePolyFromOccur(const BooleMonomial& mono, size_t eq_idx);
    void addPolyToOccur(const BoolePolynomial& poly, size_t eq_idx);
    void removePolyFromOccur(const BoolePolynomial& poly, size_t eq_idx);
    void removeEquations(std::vector<size_t>& eq2r);
    bool updateEquations(size_t idx, const BoolePolynomial newpoly,
                         vector<size_t>& empty_equations,
                         const vector<Lineral>* newfactors = nullptr);
    /// factors of eq idx after the replacer's current substitutions
    enum SubstResult { subst_unknown, subst_product, subst_zero, subst_unsat, subst_linear };
    SubstResult substituted_factors(size_t idx, vector<Lineral>& out);
    bool eraseKey(size_t idx);
    bool insertKey(size_t idx);
    void checkSimplifiedPolysContainNoSetVars() const;
    bool containsMono(const BooleMonomial& mono1,
                      const BooleMonomial& mono2) const;
    bool rewrite_eq(size_t idx, const BoolePolynomial& newpoly,
                    unordered_set<uint32_t>& updatedVars,
                    vector<size_t>& empty_equations);
    // product-preserving mode (config.keepFactor): true if `from` is a product of
    // >= 2 linerals and `to` is nonlinear but not such a product
    bool breaks_product(const BoolePolynomial& from, const BoolePolynomial& to) const;
    int keep_factor = -1; // -1: not decided yet (config.keepFactor == 2)
    bool finish_rewrites(unordered_set<uint32_t>& updatedVars,
                         vector<size_t>& empty_equations);

    //Config
    const polybori::BoolePolyRing* ring;
    ConfigData& config;

    //Comments from ANF file
    vector<string> comments;

    // Independent variables
    set<size_t> proj_set;

    //State. An equation is either a polynomial (poly_valid) or a product
    //of linear factors (factors non-empty) whose polynomial is only built
    //when asked for; products of long factors have thousands of terms.
    mutable vector<BoolePolynomial> eqs;
    mutable vector<char> poly_valid;
    vector<vector<Lineral> > factors; // parallel to eqs, see getFactors()
    vector<size_t> eq_len; // parallel to eqs: number of terms (product_size() for products)
    eqs_hash_t eqs_hash;   // hashes of the polynomial equations
    std::unordered_set<VarVec, VarVecHash> prod_keys; // keys of the product equations
    Replacer* replacer;
    vector<vector<size_t> > occur; //occur[var] -> index of polys where the variable occurs

    size_t new_equations_begin = 0;

    // nesting depth of the SimpStatsScope objects currently alive
    unsigned stats_depth = 0;
    friend class SimpStatsScope;

    friend std::ostream& operator<<(std::ostream& os, const ANF& anf);
};

inline ANF::ANF(const ANF& other, const anf_no_replacer_tag)
    : ring(other.ring),
      config(other.config),
      comments(other.comments),
      eqs(other.eqs),
      poly_valid(other.poly_valid),
      factors(other.factors),
      eq_len(other.eq_len),
      eqs_hash(other.eqs_hash),
      prod_keys(other.prod_keys),
      replacer(nullptr),
      occur(other.occur),
      new_equations_begin(other.new_equations_begin)
{
}

inline size_t ANF::size() const
{
    return eqs.size();
}

inline const BoolePolyRing& ANF::getRing() const
{
    return *ring;
}

inline size_t ANF::numMonoms() const
{
    size_t num = 0;
    for (const size_t l : eq_len) num += l;
    return num;
}

inline bool ANF::containsMono(const BooleMonomial& mono1,
                              const BooleMonomial& mono2) const
{
    return mono1.reducibleBy(mono2);
}

inline size_t ANF::deg() const
{
    int deg = 0;
    for (size_t i = 0; i < eqs.size(); i++) {
        deg = std::max(deg, degOf(i));
    }
    return deg;
}

inline const vector<BoolePolynomial>& ANF::getEqs() const
{
    for (size_t i = 0; i < eqs.size(); i++) eq(i);
    return eqs;
}

inline const BoolePolynomial& ANF::eq(size_t idx) const
{
    if (!poly_valid[idx]) {
        eqs[idx] = expand_linerals(*ring, factors[idx]);
        poly_valid[idx] = 1;
    }
    return eqs[idx];
}

inline int ANF::degOf(size_t idx) const
{
    if (poly_valid[idx]) return eqs[idx].deg();
    return factors[idx].size();
}

inline const ANF::eqs_hash_t& ANF::getEqsHash(void) const
{
    return eqs_hash;
}

inline bool ANF::hasPolynomial(const BoolePolynomial& p) const
{
    return eqs_hash.find(p.hash()) != eqs_hash.end();
}

inline size_t ANF::getNumSimpleXors() const
{
    size_t num = 0;
    for (size_t i = 0; i < eqs.size(); i++) {
        num += (degOf(i) == 1);
    }
    return num;
}

inline const vector<vector<size_t> >& ANF::getOccur() const
{
    return occur;
}

inline std::ostream& operator<<(std::ostream& os, const ANF& anf)
{
    // Dump comments
    for (const string& comment : anf.comments) {
        os << comment << endl;
    }

    // Print equations
    for (size_t i = 0; i < anf.eqs.size(); i++) {
        os << anf.eq(i);
        os << endl;
    }

    os << *(anf.replacer);
    return os;
}

inline void ANF::printStats() const
{
    cout << "c ---- ANF stats -----" << endl
         << "c Num total vars: " << getNumVars() << endl
         << "c Num free vars: " << replacer->getNumUnknownVars() << endl
         << "c Num equations: " << size() << endl
         << "c Num monoms in eqs: " << numMonoms() << endl
         << "c Max deg in eqs: " << deg() << endl
         << "c Simple XORs: " << getNumSimpleXors() << endl
         << "c Num vars set: " << getNumSetVars() << endl
         << "c Num vars replaced: " << getNumReplacedVars() << endl
         << "c --------------------" << endl;
}

vector<lbool> ANF::extendSolution(const vector<lbool>& solution) const
{
    return replacer->extendSolution(solution);
}

size_t ANF::getNumVars() const
{
    return replacer->getNumVars();
}

size_t ANF::getNumReplacedVars() const
{
    return replacer->getNumReplacedVars();
}

size_t ANF::getNumSetVars() const
{
    return replacer->getNumSetVars();
}

bool ANF::getOK() const
{
    return replacer->getOK();
}

void ANF::setNOTOK()
{
    replacer->setNOTOK();
}

lbool ANF::value(const uint32_t var) const
{
    return replacer->getValue(var);
}

Lit ANF::getReplaced(const uint32_t var) const
{
    return replacer->getReplaced(var);
}

const vector<lbool>& ANF::getFixedValues() const
{
    return replacer->getValues();
}

ANF& ANF::operator=(const ANF& other)
{
    //assert(updatedVars.empty() && other.updatedVars.empty());
    eqs = other.eqs;
    poly_valid = other.poly_valid;
    factors = other.factors;
    eq_len = other.eq_len;
    prod_keys = other.prod_keys;
    *replacer = *other.replacer;
    occur = other.occur;
    return *this;
}

}
