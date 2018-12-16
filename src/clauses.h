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

#ifndef _CLAUSES_H_
#define _CLAUSES_H_

#include <limits>
#include <vector>
#include "assert.h"
#include "cryptominisat5/solvertypesmini.h"
using CMSat::lbool;
using CMSat::Lit;
using std::vector;

class XorClause {
   public:
    explicit XorClause(const vector<uint32_t>& _vars,
                       const bool _constant = false)
        : constant(_constant) {
        for (vector<uint32_t>::const_iterator it = _vars.begin(),
                                              end = _vars.end();
             it != end; it++) {
            assert(*it != std::numeric_limits<uint32_t>::max());
            *this ^= XorClause(*it);
        }
    }

    explicit XorClause(
        const uint32_t var = std::numeric_limits<uint32_t>::max(),
        const bool _equalTrue = false)
        : constant(_equalTrue) {
        if (var != std::numeric_limits<uint32_t>::max())
            vars.push_back(var);
    }

    size_t size() const {
        return vars.size();
    }

    XorClause operator^(const XorClause& other) const {
        assert(other.sortedUnique());
        assert(sortedUnique());

        XorClause cl(std::numeric_limits<uint32_t>::max(),
                     constant ^ other.constant);

        for (vector<uint32_t>::const_iterator it1 = vars.begin(),
                                              end1 = vars.end(),
                                              it2 = other.vars.begin(),
                                              end2 = other.vars.end();
             it1 != end1 || it2 != end2;) {
            //it1 is finished, add it2
            if (it1 == end1) {
                cl.vars.push_back(*it2);
                it2++;
                continue;
            }

            //it2 is finished, add it1
            if (it2 == end2) {
                cl.vars.push_back(*it1);
                it1++;
                continue;
            }

            //Neither one is finished
            assert(it1 != end1);
            assert(it2 != end2);

            //*it1 is strictly smaller than *it2, so add *it1
            if (*it1 < *it2) {
                cl.vars.push_back(*it1);
                it1++;
                continue;
            }

            //*it2 is strictly smaller than *it1, so add *it2
            if (*it1 > *it2) {
                cl.vars.push_back(*it2);
                it2++;
                continue;
            }

            //They are the same, so they knock each other out
            assert(*it1 == *it2);
            it1++;
            it2++;
        }

        return cl;
    }

    XorClause& operator^=(const XorClause& other) {
        *this = (*this ^ other);
        return *this;
    }

    bool empty() const {
        return vars.empty();
    }

    bool getConst() const {
        return constant;
    }

    bool sortedUnique() const {
        uint32_t lastVar = var_Undef;
        for (vector<uint32_t>::const_iterator it = vars.begin(),
                                              end = vars.end();
             it != end; it++) {
            if (*it == lastVar)
                return false; // not unique
            if (lastVar != var_Undef && *it < lastVar)
                return false; // not sorted in ascending order
            lastVar = *it;
        }

        return true;
    }

    vector<uint32_t> getClause() const {
        return vars;
    }

    friend std::ostream& operator<<(std::ostream& os, const XorClause& xcl);

   private:
    vector<uint32_t> vars;
    bool constant;
};

inline std::ostream& operator<<(std::ostream& os, const XorClause& xcl) {
    if (xcl.empty()) {
        if (xcl.getConst())
            os << "0";
        return os;
    }

    os << "x";
    if (!xcl.constant)
        os << "-";
    for (vector<uint32_t>::const_iterator it = xcl.vars.begin(),
                                          end = xcl.vars.end();
         it != end; it++) {
        os << (*it + 1) << " ";
    }
    os << "0";

    return os;
}

class Clause {
   public:
    Clause(const vector<Lit>& _lits) : lits(_lits) {
    }

    const vector<Lit>& getLits() const {
        return lits;
    }

    size_t size() const {
        return lits.size();
    }

    bool empty() const {
        return lits.empty();
    }

    vector<Lit> getClause() const {
        assert(!lits.empty());
        return lits;
    }

    friend std::ostream& operator<<(std::ostream& os, const Clause& cl);

   private:
    vector<Lit> lits;
};

inline std::ostream& operator<<(std::ostream& os, const Clause& cl) {
    for (vector<Lit>::const_iterator it = cl.lits.begin(), end = cl.lits.end();
         it != end; it++) {
        os << *it << " ";
    }
    os << "0";

    return os;
}

#endif //_CLAUSES_H_
