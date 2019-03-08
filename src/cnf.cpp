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

#include "cnf.hpp"
#include <iostream>
#include <iterator>
#include <ostream>

#include "dimacscache.hpp"
#include "karnaugh.hpp"

CNF::CNF(const ANF& _anf, const vector<Clause>& cutting_clauses,
         const ConfigData& _config)
    : anf(_anf), config(_config)
{
    //Make sure outside var X is inside var X
    init();

    if (config.readCNF) {
        addOriginalCNF(cutting_clauses);
    } else {
        addAllEquations();
    }
}

size_t CNF::update(void)
{
    size_t prev = clauses.size();

    // Add new replaced and set variables to CNF
    addTrivialEquations();

    if (!config.readCNF) {
        // Because ANF may have changed to give better clauses
        const vector<BoolePolynomial>& eqs = anf.getEqs();
        for (const BoolePolynomial& poly : eqs) {
            addBoolePolynomial(poly);
        }
    }
    return prev;
}

void CNF::init()
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

void CNF::addAllEquations()
{
    // Add replaced and set variables to CNF
    addTrivialEquations();

    // Add regular equations
    const vector<BoolePolynomial>& eqs = anf.getEqs();
    for (const BoolePolynomial& poly : eqs) {
        addBoolePolynomial(poly);
    }
}

void CNF::addOriginalCNF(const vector<Clause>& cutting_clauses)
{
    // Add replaced and set variables to CNF
    addTrivialEquations();

    DIMACSCache dimacs_cache(config.cnfInput.c_str());
    vector<Clause> setOfClauses(dimacs_cache.getClauses());
    // add cutting clauses
    setOfClauses.insert(setOfClauses.end(), cutting_clauses.begin(),
                        cutting_clauses.end());

    // Associate with 0=0 polynomial
    BoolePolynomial eq(0, anf.getRing());
    clauses.push_back(std::make_pair(setOfClauses, eq));
}

void CNF::addTrivialEquations()
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

bool CNF::tryAddingPolyWithKarn(const BoolePolynomial& eq, vector<Clause>& setOfClauses) const
{
    Karnaugh karn(config.maxKarnTableSize);
    if ( !(eq.deg() > 1 && karn.possibleToConv(eq)) ) {
        return false;
    }

    setOfClauses = karn.convert(eq);

    // Estimate CNF cost
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

void CNF::addMonomialsFromPoly(const BoolePolynomial& eq)
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

void CNF::addBoolePolynomial(const BoolePolynomial& poly)
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
    if( tryAddingPolyWithKarn(poly, setOfClauses) ) {
            addedAsCNF++;
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
        addPolyWithCuts(poly, setOfClauses);
    }
    
    clauses.push_back(make_pair(setOfClauses, poly));
}

BoolePolynomial CNF::addToPolyVarsUntilCutoff(const BoolePolynomial& poly,
					      vector<uint32_t>& vars) const
{
    BoolePolynomial thisPoly(getANFRing());
    for (BoolePolynomial::const_iterator it = poly.begin(), end = poly.end();
         it != end && vars.size() < config.cutNum; it++) {
        const BooleMonomial& m = *it;

        //We will deal with the +1 given cl.getConst()
        if (m.deg() == 0)
            continue;

	//Update to CNF (external) variable numbers
        const auto findIt = monomMap.find(it->hash());
        assert(findIt !=
               monomMap.end()); //We have added all monoms once we are here

        vars.push_back(findIt->second);
	
	thisPoly += m;
    }

    return thisPoly;
}

void CNF::addPolyWithCuts(BoolePolynomial poly, vector<Clause>& setOfClauses)
// This is not a const function because it creates new cutting variables
// changes input arguments (both local and reference)
{
    assert(config.cutNum > 1);

    uint32_t varAdded = std::numeric_limits<uint32_t>::max();
    BoolePolynomial uptoPoly(getANFRing());
    while ( !poly.isConstant() ) {        
        //Add variables to clause
        vector<uint32_t> vars_in_xor;
	if (varAdded != std::numeric_limits<uint32_t>::max()) {
	  // continue from the cutting points
	  vars_in_xor.push_back(varAdded);
	}
	
        BoolePolynomial thisPoly = addToPolyVarsUntilCutoff(poly, vars_in_xor);
        poly -= thisPoly;
 	
        bool result = false;
	if ( poly.isConstant() ) {
	    result = poly.isOne();
        } else {
            //If have to cut, create new var
	    varAdded = next_cnf_var;
            next_cnf_var++;

	    //new cnf variable represents uptoPoly
	    uptoPoly += thisPoly;
	    
            //add the representative var (if appropriate)
            assert(revCombinedMap.size() == varAdded);
            assert(!uptoPoly.isSingleton());
            revCombinedMap.push_back(uptoPoly);

            vars_in_xor.push_back(
                varAdded); //This will be the definition of the representive
        }
	
        addEveryCombination(vars_in_xor, result, setOfClauses);
    }
}

uint32_t CNF::hammingWeight(uint64_t num) const
{
    uint32_t ret = 0;
    for (uint32_t i = 0; i < 64; i++) {
        ret += ((num >> i) & 1UL);
    }

    return ret;
}

void CNF::addEveryCombination(vector<uint32_t>& vars, bool isTrue,
                              vector<Clause>& thisClauses) const
{
    const uint64_t max = 1UL << vars.size();
    for (uint32_t i = 0; i < max; i++) {
        //even hamming weight -> it is true
        if (hammingWeight(i) % 2 == isTrue)
            continue;

        vector<Lit> lits;
        for (size_t i2 = 0; i2 < vars.size(); i2++) {
            const bool sign = (i >> i2) & 1;
            lits.push_back(Lit(vars[i2], sign));
        }
        thisClauses.push_back(Clause(lits));
    }
}

uint32_t CNF::addBooleMonomial(const BooleMonomial& m)
{
    if (m.isConstant()) {
        cout << "The CNF class doesn't handle adding BooleMonomials that are "
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

vector<lbool> CNF::mapSolToOrig(const std::vector<lbool>& solution) const
{
    vector<lbool> ret;
    if (solution.size() != next_cnf_var) {
        std::cerr << "ERROR: The CNF gave a solution to " << solution.size()
                  << " variables but there are only " << next_cnf_var
                  << " variables according to our count!" << endl;
        assert(false);
    }

    for (size_t i = 0; i < solution.size(); ++i) {
        // only map monomials which are single variables
        if (revCombinedMap[i].isSingleton()) {
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

BooleMonomial CNF::getMonomForVar(const uint32_t& var) const
{
    if (revCombinedMap[var].isSingleton() )
        return revCombinedMap[var].lead();
    else
        return BooleMonomial(anf.getRing());
}

uint32_t CNF::getVarForMonom(const BooleMonomial& mono) const
{
    return monomMap.find(mono.hash())->second;
}

uint64_t CNF::getNumAllLits() const
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

uint64_t CNF::getNumAllClauses() const
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
