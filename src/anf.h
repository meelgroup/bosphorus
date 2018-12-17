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

#ifndef __ANF_H__
#define __ANF_H__

#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "configdata.h"
#include "evaluator.h"
#include "polybori.h"
#include "replacer.h"

USING_NAMESPACE_PBORI

class Replacer;

using std::cout;
using std::endl;
using std::list;
using std::make_pair;
using std::map;
using std::pair;
using std::set;
using std::string;
using std::unordered_set;
using std::vector;

struct anf_no_replacer_tag {};

class ANF {
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
    void learnSolution(const vector<lbool>& solution);
    void printStats() const;

    // Returns true if polynomial is new and has been added
    bool addBoolePolynomial(const BoolePolynomial& poly);
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
    const vector<BoolePolynomial>& getEqs() const;
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

   private:
    bool check_if_need_update(const BoolePolynomial& poly,
                              unordered_set<uint32_t>& updatedVars);
    void addPolyToOccur(const BooleMonomial& mono, size_t eq_idx);
    void removePolyFromOccur(const BooleMonomial& mono, size_t eq_idx);
    void addPolyToOccur(const BoolePolynomial& poly, size_t eq_idx);
    void removePolyFromOccur(const BoolePolynomial& poly, size_t eq_idx);
    void removeEquations(std::vector<size_t>& eq2r);
    void checkSimplifiedPolysContainNoSetVars() const;
    bool containsMono(const BooleMonomial& mono1,
                      const BooleMonomial& mono2) const;

    //Config
    const polybori::BoolePolyRing* ring;
    ConfigData& config;

    //Comments from ANF file
    vector<string> comments;

    //State
    vector<BoolePolynomial> eqs;
    eqs_hash_t eqs_hash;
    Replacer* replacer;
    vector<vector<size_t> >
        occur; //occur[var] -> index of polys where the variable occurs

    size_t new_equations_begin;

    friend std::ostream& operator<<(std::ostream& os, const ANF& anf);
};

inline ANF::ANF(const ANF& other, const anf_no_replacer_tag)
    : ring(other.ring),
      config(other.config),
      comments(other.comments),
      eqs(other.eqs),
      eqs_hash(other.eqs_hash),
      replacer(nullptr),
      occur(other.occur),
      new_equations_begin(other.new_equations_begin) {
}

inline size_t ANF::size() const {
    return eqs.size();
}

inline const BoolePolyRing& ANF::getRing() const {
    return *ring;
}

inline size_t ANF::numMonoms() const {
    size_t num = 0;
    for (const BoolePolynomial& poly : eqs) {
        num += poly.length();
    }
    return num;
}

inline bool ANF::containsMono(const BooleMonomial& mono1,
                              const BooleMonomial& mono2) const {
    return mono1.reducibleBy(mono2);
}

inline size_t ANF::deg() const {
    int deg = 0;
    for (const BoolePolynomial& poly : eqs) {
        deg = std::max(deg, poly.deg());
    }
    return deg;
}

inline const vector<BoolePolynomial>& ANF::getEqs() const {
    return eqs;
}

inline const ANF::eqs_hash_t& ANF::getEqsHash(void) const {
    return eqs_hash;
}

inline bool ANF::hasPolynomial(const BoolePolynomial& p) const {
    return eqs_hash.find(p.hash()) != eqs_hash.end();
}

inline size_t ANF::getNumSimpleXors() const {
    size_t num = 0;
    for (const BoolePolynomial& poly : eqs) {
        num += (poly.deg() == 1);
    }
    return num;
}

inline const vector<vector<size_t> >& ANF::getOccur() const {
    return occur;
}

inline std::ostream& operator<<(std::ostream& os, const ANF& anf) {
    // Dump comments
    for (const string& comment : anf.comments) {
        os << comment << endl;
    }

    // Print equations
    for (const BoolePolynomial& poly : anf.eqs) {
        os << poly;
        os << endl;
    }

    os << *(anf.replacer);
    return os;
}

inline void ANF::printStats() const {
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

vector<lbool> ANF::extendSolution(const vector<lbool>& solution) const {
    return replacer->extendSolution(solution);
}

size_t ANF::getNumVars() const {
    return replacer->getNumVars();
}

size_t ANF::getNumReplacedVars() const {
    return replacer->getNumReplacedVars();
}

size_t ANF::getNumSetVars() const {
    return replacer->getNumSetVars();
}

bool ANF::getOK() const {
    return replacer->getOK();
}

void ANF::setNOTOK() {
    replacer->setNOTOK();
}

lbool ANF::value(const uint32_t var) const {
    return replacer->getValue(var);
}

Lit ANF::getReplaced(const uint32_t var) const {
    return replacer->getReplaced(var);
}

const vector<lbool>& ANF::getFixedValues() const {
    return replacer->getValues();
}

ANF& ANF::operator=(const ANF& other) {
    //assert(updatedVars.empty() && other.updatedVars.empty());
    eqs = other.eqs;
    *replacer = *other.replacer;
    occur = other.occur;
    return *this;
}

bool extendedLinearization(const ConfigData& config,
                           const vector<BoolePolynomial>& eqs,
                           vector<BoolePolynomial>& loop_learnt);

#endif //__ANF_H__
