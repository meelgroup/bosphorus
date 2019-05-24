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

#ifndef REPLACER_H__
#define REPLACER_H__

#include <map>
#include <set>
#include <vector>
#include "bosphorus/solvertypesmini.hpp"
#include "polybori.h"

USING_NAMESPACE_PBORI
using namespace Bosph;

using std::map;
using std::set;
using std::vector;

namespace BLib {

class ANF;

class Replacer
{
   public:
    Replacer() : ok(true)
    {
    }

    void newVar(const uint32_t var)
    {
        assert(value.size() == var);
        value.push_back(l_Undef);

        assert(replaceTable.size() == var);
        replaceTable.push_back(Lit(var, false));
    }

    //returns updated vars
    vector<uint32_t> setValue(uint32_t var, bool value);
    vector<uint32_t> setReplace(uint32_t var, Lit lit);
    BoolePolynomial update(const BooleMonomial& m) const;
    BoolePolynomial update(const BoolePolynomial& m) const;

    bool willUpdate(const BoolePolynomial& m) const;
    bool evaluate(const vector<lbool>& vals) const;
    vector<lbool> extendSolution(const vector<lbool>& solution) const;
    void setNOTOK();
    void print_solution_map(std::ofstream* ofs);

    //Get-functions
    lbool getValue(const uint32_t var) const;
    const vector<lbool>& getValues() const;
    Lit getReplaced(const uint32_t var) const;
    bool isReplaced(const uint32_t var) const;
    bool getOK() const;
    size_t getNumVars() const;
    size_t getNumUnknownVars() const;
    size_t getNumReplacedVars() const;
    size_t getNumSetVars() const;
    vector<uint32_t> getReplacesVars(const uint32_t var) const;

   private:
    //uint32_t-to-equi/antivalent var
    vector<lbool> value;
    vector<Lit> replaceTable;
    map<uint32_t, vector<uint32_t> > revReplaceTable;

    //state
    bool ok;

    friend std::ostream& operator<<(std::ostream& os, const Replacer& repl);
};

inline lbool Replacer::getValue(const uint32_t var) const
{
    assert(value.size() > var);
    return value[var];
}

inline const vector<lbool>& Replacer::getValues() const
{
    return value;
}

inline Lit Replacer::getReplaced(const uint32_t var) const
{
    assert(replaceTable.size() > var);
    return replaceTable[var];
}

inline vector<uint32_t> Replacer::getReplacesVars(const uint32_t var) const
{
    auto it = revReplaceTable.find(var);
    if (it == revReplaceTable.end()) {
        return vector<uint32_t>();
    } else {
        return it->second;
    }
}

inline void Replacer::setNOTOK()
{
    ok = false;
}

inline bool Replacer::getOK() const
{
    return ok;
}

inline size_t Replacer::getNumVars() const
{
    return value.size();
}

inline size_t Replacer::getNumUnknownVars() const
{
    size_t ret = 0;
    size_t num = 0;
    for (vector<Lit>::const_iterator it = replaceTable.begin(),
                                     end = replaceTable.end();
         it != end; it++, num++) {
        if (num == it->var() && value[num] == l_Undef)
            ret++;
    }

    return ret;
}

inline size_t Replacer::getNumReplacedVars() const
{
    size_t ret = 0;
    size_t num = 0;
    for (vector<Lit>::const_iterator it = replaceTable.begin(),
                                     end = replaceTable.end();
         it != end; it++, num++) {
        if (num != it->var())
            ret++;
    }

    return ret;
}

inline size_t Replacer::getNumSetVars() const
{
    size_t ret = 0;
    for (vector<lbool>::const_iterator it = value.begin(), end = value.end();
         it != end; it++) {
        if (*it != l_Undef)
            ret++;
    }

    return ret;
}

inline std::ostream& operator<<(std::ostream& os, const Replacer& repl)
{
    //print values
    os << "c -------------" << std::endl;
    os << "c Fixed values" << std::endl;
    os << "c -------------" << std::endl;
    uint32_t num = 0;
    for (vector<lbool>::const_iterator it = repl.value.begin(),
                                       end = repl.value.end();
         it != end; it++, num++) {
        if (*it == l_Undef)
            continue;

        os << "x(" << num << ")";
        if (*it == l_True)
            os << " + 1";
        os << std::endl;
    }

    //print equivalences
    os << "c -------------" << std::endl;
    os << "c Equivalences" << std::endl;
    os << "c -------------" << std::endl;
    num = 0;
    for (vector<Lit>::const_iterator it = repl.replaceTable.begin(),
                                     end = repl.replaceTable.end();
         it != end; it++, num++) {
        if (*it == Lit(num, false) || repl.getValue(num) != l_Undef)
            continue;

        os << "x(" << num << ") + x(" << it->var() << ")";
        if (it->sign())
            os << " + 1";
        os << std::endl;
    }

    //If UNSAT, it's easy to indicate that: 1 = 0
    if (!repl.ok) {
        os << "c -------------" << std::endl;
        os << "c because of Fixed & Equivalences, it is UNSAT" << std::endl;
        os << "c -------------" << std::endl;
        os << "1" << std::endl;
    }
    os << "c UNSAT : " << std::boolalpha << !repl.ok << std::endl;

    return os;
}

}

#endif //__REPLACER_H__
