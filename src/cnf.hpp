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

#ifndef CNF_H__
#define CNF_H__

#include <boost/variant.hpp>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "anf.hpp"
#include "bosphorus/solvertypesmini.hpp"

namespace BLib {

class CNF
{
   public:
    CNF(const ANF& _anf, const ConfigData& _config);
    CNF(const char* fname, const ANF& _anf,
        const vector<Clause>& clauses_needed_for_anf_import,
        const ConfigData& _config);

    /// update CNF with new equations and facts from the anf-sibling;
    /// @returns the previous number of clause-sets
    size_t update();

    // Remap solution to CNF to a solution for the original ANF
    vector<lbool> mapSolToOrig(const std::vector<lbool>& solution) const;

    void printStats() const;

    // Get functions
    const BoolePolyRing& getANFRing(void) const
    {
        return anf.getRing();
    }
    void print_solution_map(std::ofstream* ofs);
    void write_projection_set(std::ofstream* ofs) const;
    void get_solution_map(map<uint32_t, VarMap>& ret) const;
    bool varRepresentsMonomial(const uint32_t var) const;
    BooleMonomial getMonomForVar(const uint32_t& var) const;
    uint32_t getVarForMonom(const BooleMonomial& mono) const;
    size_t getNumClauses() const;
    size_t getAddedAsCNF() const;
    size_t getAddedAsANF() const;
    size_t getAddedAsSimpleANF() const;
    size_t getAddedAsComplexANF() const;
    const vector<pair<vector<Clause>, BoolePolynomial> >& getClauses() const;
    vector<Clause> get_clauses_simple() const;
    uint32_t getNumVars() const;
    uint64_t getNumAllLits() const;
    uint64_t getNumAllClauses() const;

    friend std::ostream& operator<<(std::ostream& os, const CNF& cnf);
    void print_without_header(std::ostream& os) const;

   private:
    void init();
    void addBoolePolynomial(const BoolePolynomial& eq);
    void addTrivialEquations();
    bool tryAddingPolyWithKarn(const BoolePolynomial& eq,
                               vector<Clause>& setOfClauses) const;
    void addMonomialsFromPoly(const BoolePolynomial& eq);
    BoolePolynomial addToPolyVarsUntilCutoff(const BoolePolynomial& poly,
                                             vector<uint32_t>& vars) const;

    //Main adders
    uint32_t addBooleMonomial(const BooleMonomial& m);

    //Adding by enumeration (with cuts)
    void addPolyWithCuts(BoolePolynomial poly, vector<Clause>& setOfClauses);
    uint32_t hammingWeight(uint64_t num) const;
    void addEveryCombination(vector<uint32_t>& vars, bool isTrue,
                             vector<Clause>& thisClauses) const;

    //Setup
    const ANF& anf;
    const ConfigData& config;

    //The cumulated CNF data
    vector<pair<vector<Clause>, BoolePolynomial> > clauses;
    ANF::eqs_hash_t in_clauses;

    //uint32_t maps -- internal/external mapping of variables/monomial/polynomials
    std::unordered_map<BooleMonomial::hash_type, uint32_t>
        monomMap; // map: outside monom -> inside var
    vector<BoolePolynomial>
        revCombinedMap; // combines monomial map and xor maps; // map: inside var -> outside monom; // When cutting XORs, which var represents which XOR cut. Poly is of degree 1 here of course
    uint32_t next_cnf_var = 0; ///<CNF variable counter

    //stats
    size_t addedAsANF = 0;
    size_t addedAsSimpleANF = 0;
    size_t addedAsComplexANF = 0;
    size_t addedAsCNF = 0;
};

inline void CNF::print_without_header(std::ostream& os) const
{
    for (const auto& set_of_cls: clauses) {
        os << set_of_cls.first;
        if (config.writecomments) {
            os << "c " << set_of_cls.second << std::endl;
            os << "c ------------\n";
        }
    }
}

inline std::ostream& operator<<(std::ostream& os, const CNF& cnf)
{
    os << "p cnf " << cnf.getNumVars() << " " << cnf.getNumAllClauses()
       << std::endl;
    cnf.print_without_header(os);
    return os;
}

inline bool CNF::varRepresentsMonomial(const uint32_t var) const
{
    return revCombinedMap[var].isSingleton();
}

inline size_t CNF::getNumClauses() const
{
    return clauses.size();
}

inline size_t CNF::getAddedAsCNF() const
{
    return addedAsCNF;
}

inline size_t CNF::getAddedAsANF() const
{
    return addedAsANF;
}

inline size_t CNF::getAddedAsSimpleANF() const
{
    return addedAsSimpleANF;
}

inline size_t CNF::getAddedAsComplexANF() const
{
    return addedAsComplexANF;
}

inline const vector<pair<vector<Clause>, BoolePolynomial> >& CNF::getClauses()
    const
{
    return clauses;
}

inline uint32_t CNF::getNumVars() const
{
    return next_cnf_var;
}

inline void CNF::printStats() const
{
    cout << "c ---- CNF stats -----" << endl
         << "c Map sizes            : " << monomMap.size() << '/'
         << revCombinedMap.size() << endl
         << "c Clause Sets          : " << getNumClauses() << endl
         << "c Added as CNF         : " << getAddedAsCNF() << endl
         << "c Added as simple ANF  : " << getAddedAsSimpleANF() << endl
         << "c Added as complex  ANF: " << getAddedAsComplexANF() << endl
         << "c --------------------" << endl;
}

}

#endif //CNF_H__
