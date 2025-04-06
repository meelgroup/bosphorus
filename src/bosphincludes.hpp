/*****************************************************************************
Copyright (C) 2019  Kian Ming A. Chai, DSO National Laboratories

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

#include <vector>
#include <iostream>
#include <cassert>
#include <cryptominisat5/solvertypesmini.h>

using std::vector;
using std::ostream;
using std::endl;
using std::cout;
using std::cerr;
using CMSat::Lit;
using CMSat::lbool;
using CMSat::l_Undef;
using CMSat::l_True;
using CMSat::l_False;

namespace Bosph {

class Solution
{
public:
    vector<lbool> sol;
    lbool ret = l_Undef;
};

class Clause
{
   public:
    Clause(const vector<Lit>& _lits) : lits(_lits)
    {
    }

    const vector<Lit>& getLits() const
    {
        return lits;
    }

    size_t size() const
    {
        return lits.size();
    }

    bool empty() const
    {
        return lits.empty();
    }

    std::vector<Lit> getClause() const
    {
        assert(!lits.empty());
        return lits;
    }

    friend std::ostream& operator<<(std::ostream& os, const Clause& cl);

    vector<Lit> lits;
};

struct VarMap
{
    enum {cnf_var, anf_repl, must_set, fixed} type;
    uint32_t other_var;
    bool inv;
    bool value;
};

inline std::ostream& operator<<(std::ostream& os, const Clause& cl)
{
    for (vector<Lit>::const_iterator it = cl.lits.begin(), end = cl.lits.end();
         it != end; it++) {
        os << *it << " ";
    }
    os << "0";

    return os;
}

inline std::ostream& operator<<(std::ostream& os, const vector<Clause>& clauses)
{
    for (const auto& it: clauses) {
        os << it << std::endl;
    }
    return os;
}

}
