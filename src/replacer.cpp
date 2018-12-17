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

#include <iostream>
#include "replacer.h"
#include "anf.h"

using CMSat::lbool;
using CMSat::Lit;

using std::cout;
using std::endl;

bool Replacer::evaluate(const vector<lbool>& vals) const
{
    bool ret = true;
    size_t num = 0;
    for (vector<lbool>::const_iterator it = value.begin(), end = value.end();
         it != end; it++, num++) {
        if (*it == l_Undef)
            continue;

        assert(vals.size() >= num);
        ret &= (vals[num] == *it);
    }

    num = 0;
    for (vector<Lit>::const_iterator it = replaceTable.begin(),
                                     end = replaceTable.end();
         it != end; it++, num++) {
        if (num == it->var())
            continue;

        assert(vals.size() >= it->var());
        assert(vals.size() >= num);
        lbool one = vals[it->var()] ^ it->sign();
        lbool other = vals[num];
        ret &= (one == other);
    }

    return ret;
}

/*
 * Suppose monomial is a*b*c and we want to replace:
 *   a with 1+d
 *   b with e
 *   c with 1+f
 * (Note: e may be b itself)
 *
 * init   : ret = 1
 * after a: ret = (d) + (1)
 *              = d + 1
 * after b: ret = e*d + e
 * after c: ret = (f*e*d + f*e) + (e*d + e)
 *              = f*e*d + f*e + e*d + e
 *              = (1+d)*(e)*(1+f)
 *              = a*b*c
 */
BoolePolynomial Replacer::update(const BooleMonomial& m) const
{
    BoolePolynomial ret(true, m.ring());

    for (const uint32_t v : m) {
        if (value[v] != l_Undef) {
            if (value[v] == l_True)
                continue;
            else
                return BoolePolynomial(m.ring());
        }

        assert(replaceTable.size() > v); //Variable must exist
        const Lit lit = replaceTable[v];

        BoolePolynomial alsoAdd(m.ring());
        if (lit.sign())
            alsoAdd = ret;
        ret *= BooleVariable(lit.var(), m.ring());
        ret += alsoAdd;
    }

    return ret;
}

BoolePolynomial Replacer::update(const BoolePolynomial& eq) const
{
    BoolePolynomial ret = BoolePolynomial(eq.ring());
    for (const BooleMonomial& mono : eq) {
        ret += update(mono);
    }
    return ret;
}

bool Replacer::willUpdate(const BoolePolynomial& eq) const
{
    for (const uint32_t v : eq.usedVariables()) {
        if (value[v] != l_Undef)
            return true;
        if (replaceTable[v] != Lit(v, false))
            return true;
    }
    return false;
}

vector<uint32_t> Replacer::setValue(uint32_t var, bool val)
{
    vector<uint32_t> alsoUpdated;
    alsoUpdated.push_back(var);

    //update to representative
    if (replaceTable[var] != Lit(var, false)) {
        val ^= replaceTable[var].sign();
        var = replaceTable[var].var();
    }
    alsoUpdated.push_back(var);

    //set value
    if (value[var] != l_Undef) {
        if (value[var] != CMSat::boolToLBool(val)) {
            ok = false;
        }
        return alsoUpdated;
    }

    value[var] = CMSat::boolToLBool(val);

    //update anti/equivalent variables
    map<uint32_t, vector<uint32_t> >::iterator it = revReplaceTable.find(var);
    if (it == revReplaceTable.end()) {
        return alsoUpdated;
    }

    vector<uint32_t>& vars = it->second;
    for (vector<uint32_t>::iterator it2 = vars.begin(), end2 = vars.end();
         it2 != end2; it2++) {
        assert(replaceTable[*it2].var() == var);
        value[*it2] = CMSat::boolToLBool(replaceTable[*it2].sign() ^ val);
        alsoUpdated.push_back(*it2);
    }

    return alsoUpdated;
}

vector<uint32_t> Replacer::setReplace(uint32_t var, Lit lit)
{
    assert(var < replaceTable.size());
    assert(lit.var() < replaceTable.size());

    vector<uint32_t> ret;

    //if value is set somewhere, set it
    if (value[var] != l_Undef) {
        return setValue(lit.var(), (value[var] ^ lit.sign()) == l_True);
    }

    if (value[lit.var()] != l_Undef) {
        return setValue(var, (value[lit.var()] ^ lit.sign()) == l_True);
    }

    //move forward
    lit ^= replaceTable[var].sign();
    var = replaceTable[var].var();
    lit = replaceTable[lit.var()] ^ lit.sign();
    ret.push_back(lit.var());
    ret.push_back(var);

    //they are already replaced with each other
    if (lit.var() == var) {
        //..but invertedly, so UNSAT
        if (lit.sign())
            ok = false;
        return ret;
    }

    //lit.var() is not the head of a replace group
    if (revReplaceTable.find(lit.var()) == revReplaceTable.end()) {
        replaceTable[lit.var()] = Lit(var, lit.sign());
        revReplaceTable[var].push_back(lit.var());

        //These may have been updated
        for (vector<uint32_t>::const_iterator it = revReplaceTable[var].begin(),
                                              end = revReplaceTable[var].end();
             it != end; it++) {
            ret.push_back(*it);
        }
        return ret;
    }

    //var is not the head of a replace group
    if (revReplaceTable.find(var) == revReplaceTable.end()) {
        replaceTable[var] = lit;
        revReplaceTable[lit.var()].push_back(var);

        //These may have been updated
        for (vector<uint32_t>::const_iterator
                 it = revReplaceTable[lit.var()].begin(),
                 end = revReplaceTable[lit.var()].end();
             it != end; it++) {
            ret.push_back(*it);
        }

        return ret;
    }

    //both are dependent, move lit's dependencies under var
    assert(revReplaceTable.find(lit.var()) != revReplaceTable.end());
    assert(revReplaceTable.find(var) != revReplaceTable.end());

    vector<uint32_t>& vars = revReplaceTable[lit.var()];
    for (vector<uint32_t>::iterator it = vars.begin(), end = vars.end();
         it != end; it++) {
        revReplaceTable[var].push_back(*it);
        replaceTable[*it] = Lit(var, replaceTable[*it].sign() ^ lit.sign());
        ret.push_back(*it);
    }
    revReplaceTable.erase(revReplaceTable.find(lit.var()));
    replaceTable[lit.var()] = Lit(var, lit.sign());
    revReplaceTable[var].push_back(lit.var());

    //These may have been updated
    for (vector<uint32_t>::const_iterator it = revReplaceTable[var].begin(),
                                          end = revReplaceTable[var].end();
         it != end; it++) {
        ret.push_back(*it);
    }

    return ret;
}

vector<lbool> Replacer::extendSolution(const vector<lbool>& solution) const
{
    assert(solution.size() <= value.size());
    vector<lbool> sol2(solution);
    sol2.resize(value.size(), l_Undef);

    //add stored solutions
    assert(sol2.size() == value.size());
    size_t num = 0;
    for (vector<lbool>::const_iterator it = value.begin(), end = value.end();
         it != end; it++, num++) {
        if (sol2[num] != l_Undef && *it != l_Undef) {
            if (sol2[num] != *it) {
                cout << "Solved solution and solution stored by replacer "
                        "differ!\n";
                exit(-1);
            }
            continue;
        }

        if (*it != l_Undef) {
            sol2[num] = *it;
        }
    }

    //Add replaced variables
    assert(sol2.size() == replaceTable.size());
    num = 0;
    for (vector<Lit>::const_iterator it = replaceTable.begin(),
                                     end = replaceTable.end();
         it != end; it++, num++) {
        //uint32_t is not replaced
        if (it->var() == num)
            continue;

        //maybe never solved for, because equation is "a = b", and neither "a", nor "b" appear anywhere else
        //so, just set the value randomly... to true :)
        if (sol2[it->var()] == l_Undef)
            sol2[it->var()] = l_True;

        const lbool val = sol2[it->var()] ^ it->sign();
        if (sol2[num] != l_Undef && sol2[num] != val) {
            cout << "num:" << num << " var:" << it->var()
                 << "sign: " << (int)it->sign() << endl;
            cout << "sol2[num]: " << sol2[num]
                 << " sol2[it->var()]:" << sol2[it->var()] << endl;
            cout << "Solved replaced solution and stored solution differ!"
                 << endl;
            exit(-1);
        }
        sol2[num] = val;
    }

    return sol2;
}

bool Replacer::isReplaced(const uint32_t var) const
{
    return getReplaced(var).var() != var;
}
