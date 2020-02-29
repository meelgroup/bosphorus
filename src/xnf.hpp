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

#ifndef XNF_H__
#define XNF_H__

#include <boost/variant.hpp>
#include <fstream>
#include <unordered_map>
#include <vector>

#include "anf.hpp"
#include "clauses.hpp"

using boost::get;
using boost::variant;

class XNF
{
   public:
    XNF(const ANF& _anf, const ConfigData& _config);

    // Remap solution to XNF to a solution for the original ANF
    vector<lbool> mapSolToOrig(const std::vector<lbool>& solution) const;

    void printStats() const;

    // Get functions
    const BoolePolyRing& getANFRing(void) const
    {
        return anf.getRing();
    }
    bool varRepresentsMonomial(const uint32_t var) const;
    BooleMonomial getMonomForVar(const uint32_t& var) const;
    uint32_t getVarForMonom(const BooleMonomial& mono) const;
    size_t getNumClauses() const;
    size_t getAddedAsXNF() const;
    size_t getAddedAsANF() const;
    size_t getAddedAsSimpleANF() const;
    size_t getAddedAsComplexANF() const;
    const vector<pair<vector<Clause>, BoolePolynomial> >& getClauses() const;
    uint32_t getNumVars() const;
    uint64_t getNumAllLits() const;
    uint64_t getNumAllClauses() const;

    friend std::ostream& operator<<(std::ostream& os, const XNF& cnf);
    void print_without_header(std::ostream& os) const;

   private:
    void init();
    void addAllEquations();
    void addBoolePolynomial(const BoolePolynomial& eq);
    void addTrivialEquations();
    bool tryAddingPolyWithKarn(const BoolePolynomial& eq,
                               vector<Clause>& setOfClauses) const;
    void addMonomialsFromPoly(const BoolePolynomial& eq);

    //Main adders
    uint32_t addBooleMonomial(const BooleMonomial& m);

    //Setup
    const ANF& anf;
    const ConfigData& config;

    //The cumulated XNF data
    vector<pair<vector<Clause>, BoolePolynomial> > clauses;
    vector<pair<XClause, BoolePolynomial> > xcls;
    ANF::eqs_hash_t in_clauses;

    //uint32_t maps -- internal/external mapping of variables/monomial/polynomials
    std::unordered_map<BooleMonomial::hash_type, uint32_t>
        monomMap; // map: outside monom -> inside var
    vector<BoolePolynomial>
        revCombinedMap; // combines monomial map and xor maps; // map: inside var -> outside monom; // When cutting XORs, which var represents which XOR cut. Poly is of degree 1 here of course
    uint32_t next_cnf_var = 0; ///<XNF variable counter

    //stats
    size_t addedAsANF = 0;
    size_t addedAsSimpleANF = 0;
    size_t addedAsComplexANF = 0;
    size_t addedAsXNF = 0;
};

inline void XNF::print_without_header(std::ostream& os) const
{
    for (const auto& it : clauses) {
        os << it.first;
        if (config.writecomments) {
            os << "c " << it.second << std::endl;
            os << "c ------------\n";
        }
    }

    for (const auto& it : xcls) {
        os << it.first;
        if (config.writecomments) {
            os << "c " << it.second << std::endl;
            os << "c ------------\n";
        }
    }
}

inline std::ostream& operator<<(std::ostream& os, const XNF& cnf)
{
    os << "p cnf " << cnf.getNumVars() << " " << cnf.getNumAllClauses()
       << std::endl;
    cnf.print_without_header(os);
    return os;
}

inline bool XNF::varRepresentsMonomial(const uint32_t var) const
{
    return revCombinedMap[var].isSingleton();
}

inline size_t XNF::getNumClauses() const
{
    return clauses.size();
}

inline size_t XNF::getAddedAsXNF() const
{
    return addedAsXNF;
}

inline size_t XNF::getAddedAsANF() const
{
    return addedAsANF;
}

inline size_t XNF::getAddedAsSimpleANF() const
{
    return addedAsSimpleANF;
}

inline size_t XNF::getAddedAsComplexANF() const
{
    return addedAsComplexANF;
}

inline const vector<pair<vector<Clause>, BoolePolynomial> >& XNF::getClauses()
    const
{
    return clauses;
}

inline uint32_t XNF::getNumVars() const
{
    return next_cnf_var;
}

inline void XNF::printStats() const
{
    cout << "c ---- XNF stats -----" << endl
         << "c Map sizes            : " << monomMap.size() << '/'
         << revCombinedMap.size() << endl
         << "c Clause Sets          : " << getNumClauses() << endl
         << "c Added as XNF         : " << getAddedAsXNF() << endl
         << "c Added as simple ANF  : " << getAddedAsSimpleANF() << endl
         << "c Added as complex  ANF: " << getAddedAsComplexANF() << endl
         << "c --------------------" << endl;
}

#endif //XNF_H__
