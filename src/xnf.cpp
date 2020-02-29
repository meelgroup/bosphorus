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

#include "xnf.hpp"
#include <iostream>
#include <iterator>
#include <ostream>

#include "dimacscache.hpp"
#include "karnaugh.hpp"

XNF::XNF(const ANF& _anf, const ConfigData& _config)
    : anf(_anf), config(_config)
{
    //Make sure outside var X is inside var X
    init();
    addAllEquations();
}

void XNF::init()
{
    monomMap.reserve(anf.getRing().nVariables());
    revCombinedMap.resize(anf.getRing().nVariables(),
                          BooleMonomial(anf.getRing()));
    assert(next_cnf_var == 0);
    for (uint32_t var = 0; var < anf.getRing().nVariables();
         ++var) { // Add ALL variables
        BooleVariable v = anf.getRing().variable(var);
        monomMap[v.hash()] = next_cnf_var;
        revCombinedMap[next_cnf_var] = BooleMonomial(v);
        next_cnf_var++;
    }

    // If ANF is not OK, then add polynomial '1'
    if (!anf.getOK()) {
        addBoolePolynomial(BoolePolynomial(true, anf.getRing()));
    }
}

void XNF::addAllEquations()
{
    // Add replaced and set variables to XNF
    addTrivialEquations();

    // Add regular equations
    const vector<BoolePolynomial>& eqs = anf.getEqs();
    for (const BoolePolynomial& poly : eqs) {
        addBoolePolynomial(poly);
    }
}

void XNF::addTrivialEquations()
{
    size_t nv = 0, nr = 0;
    for (size_t i = 0; i < anf.getRing().nVariables(); i++) {
        //Add variables already set
        if (anf.value(i) != l_Undef) {
            BoolePolynomial poly(false, anf.getRing());
            poly += BooleVariable(i, anf.getRing());
            poly += BooleConstant(anf.value(i) == l_True);
            addBoolePolynomial(poly);
            ++nv;
        }

        //Add variables replaced
        if (anf.getReplaced(i).var() != i) {
            const Lit replacedWith = anf.getReplaced(i);
            BoolePolynomial poly(false, anf.getRing());
            poly += BooleConstant(replacedWith.sign());
            poly += BooleVariable(replacedWith.var(), anf.getRing());
            poly += BooleVariable(i, anf.getRing());
            addBoolePolynomial(poly);
            ++nr;
        }
    }
    if (config.verbosity >= 4) {
        std::cout << "c Number of value assignments = " << nv
                  << "\nc Number of equiv assigments = " << nr << std::endl;
    }
}

bool XNF::tryAddingPolyWithKarn(const BoolePolynomial& eq,
                                vector<Clause>& setOfClauses) const
{
    Karnaugh karn(config.maxKarnTableSize);
    if (!(eq.deg() > 1 && karn.possibleToConv(eq))) {
        return false;
    }

    setOfClauses = karn.convert(eq);

    // Estimate XNF cost
    uint32_t cnfCost = 0;
    for (const Clause& c : setOfClauses) {
        cnfCost += c.size();
        cnfCost += 2;
    }

    // Estimate ANF cost
    uint32_t anfCost = 0;
    for (const BooleMonomial& mono : eq) {
        anfCost += mono.deg() * 2;
        anfCost += 5;
    }

    if (anfCost >= cnfCost ||
        (eq.terms().size() == (1UL + (size_t)eq.hasConstantPart()))) {
        return true;
    }

    setOfClauses.clear();
    return false;
}

void XNF::addMonomialsFromPoly(const BoolePolynomial& eq)
{
    for (BoolePolynomial::const_iterator it = eq.begin(), end = eq.end();
         it != end; it++) {
        if (it->isConstant()) {
            continue;
        } else {
            addBooleMonomial(*it);
        }
    }
}

void XNF::addBoolePolynomial(const BoolePolynomial& poly)
{
    if (!in_clauses.insert(poly.hash()).second)
        return; // is already added

    // If UNSAT, make it UNSAT
    if (poly.isOne()) {
        vector<Clause> tmp;
        vector<Lit> lits;
        tmp.push_back(Clause(lits));
        clauses.push_back(std::make_pair(tmp, poly));
    }

    if (poly.isZero()) {
        return;
    }

    vector<Clause> setOfClauses;
    if (tryAddingPolyWithKarn(poly, setOfClauses)) {
        addedAsXNF++;
        clauses.push_back(make_pair(setOfClauses, poly));
    } else {
        // Represent using XOR & monomial combination
        // 1) add monmials
        // 2) add XOR
        addedAsANF++;
        if (poly.deg() < 2) {
            addedAsSimpleANF++;
        } else {
            addedAsComplexANF++;
        }

        addMonomialsFromPoly(poly);

        vector<uint32_t> vars;
        bool rhs = true;
        for (const auto m: poly) {
            if (m.deg() == 0) {
                rhs = false;
                continue;
            }

            //Update to CNF (external) variable numbers
            const auto findIt = monomMap.find(m.hash());
            assert(findIt != monomMap.end()); //We have added all monoms once we are here
            vars.push_back(findIt->second);
        }
        XClause xcl (vars, rhs);
        xcls.push_back(make_pair(xcl, poly));
    }

}

uint32_t XNF::addBooleMonomial(const BooleMonomial& m)
{
    if (m.isConstant()) {
        cout << "The XNF class doesn't handle adding BooleMonomials that are "
                "empty"
             << std::endl;
        exit(-1);
    }

    //monomial already known, return it
    const auto it = monomMap.find(m.hash());
    if (it != monomMap.end()) {
        return it->second;
    }

    //create monomial, as well as the corresponding clauses
    const uint32_t newVar = next_cnf_var;
    monomMap[m.hash()] = next_cnf_var;
    assert(revCombinedMap.size() == next_cnf_var);
    revCombinedMap.push_back(m);
    next_cnf_var++;

    //Check that all variables exist&create m2 that is the monom in internal representation
    std::vector<uint32_t> m2;
    m2.reserve(m.deg());
    for (BooleMonomial::const_iterator it = m.begin(), end = m.end(); it != end;
         it++) {
        auto it2 = monomMap.find(BooleVariable(*it, anf.getRing()).hash());
        assert(it2 != monomMap.end());
        m2.push_back(it2->second);
    }

    vector<Clause> setOfClauses;
    //create clauses e.g. '-a b', '-a c', '-a d' , etc.
    vector<Lit> lits;
    lits.reserve(m2.size() + 1);
    for (uint32_t v : m2) {
        lits.clear();
        lits.push_back(Lit(newVar, true));
        lits.push_back(Lit(v, false));
        setOfClauses.push_back(Clause(lits));
    }

    //create final clause e.g. 'a -b -c -d'
    lits.clear();
    lits.push_back(Lit(newVar, false));
    for (uint32_t v : m2) {
        lits.push_back(Lit(v, true));
    }
    setOfClauses.push_back(Clause(lits));
    clauses.push_back(std::make_pair(setOfClauses, m));
    return newVar;
}

vector<lbool> XNF::mapSolToOrig(const std::vector<lbool>& solution) const
{
    vector<lbool> ret;
    if (solution.size() != next_cnf_var) {
        std::cerr << "ERROR: The XNF gave a solution to " << solution.size()
                  << " variables but there are only " << next_cnf_var
                  << " variables according to our count!" << endl;
        assert(false);
    }

    for (size_t i = 0; i < solution.size(); ++i) {
        // only map monomials which are single variables
        if (varRepresentsMonomial(i)) {
            const BooleMonomial& m(revCombinedMap[i].lead());

            //Only single-vars
            if (m.deg() == 1) {
                const uint32_t var = m.firstVariable().index();
                if (ret.size() <= var)
                    ret.resize(var + 1, l_Undef);
                ret[var] = solution[i];
            }
        }
    }

    return ret;
}

BooleMonomial XNF::getMonomForVar(const uint32_t& var) const
{
    if (varRepresentsMonomial(var))
        return revCombinedMap[var].lead();
    else
        return BooleMonomial(anf.getRing());
}

uint32_t XNF::getVarForMonom(const BooleMonomial& mono) const
{
    return monomMap.find(mono.hash())->second;
}

uint64_t XNF::getNumAllLits() const
{
    uint64_t numLits = 0;
    for (vector<pair<vector<Clause>, BoolePolynomial> >::const_iterator
             it = clauses.begin(),
             end = clauses.end();
         it != end; it++) {
        const vector<Clause>& thisClauses = it->first;
        for (vector<Clause>::const_iterator itCls = thisClauses.begin(),
                                            endCls = thisClauses.end();
             itCls != endCls; itCls++) {
            numLits += itCls->size();
        }
    }

    return numLits;
}

uint64_t XNF::getNumAllClauses() const
{
    uint64_t numClauses = 0;
    for (vector<pair<vector<Clause>, BoolePolynomial> >::const_iterator
             it = clauses.begin(),
             end = clauses.end();
         it != end; it++) {
        numClauses += it->first.size();
    }

    return numClauses;
}
