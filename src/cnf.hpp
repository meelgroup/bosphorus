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

#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "anf.hpp"
#include "bosphincludes.hpp"

using std::pair;

namespace BLib {

class CNF
{
   public:
    // Monomials as sorted variable-index vectors: the partner cover works on
    // these instead of ZDDs, which is much cheaper.
    typedef BLib::VarVec VarVec;
    typedef BLib::VarVecHash VarVecHash;

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
    void write_projection_set(std::ofstream* ofs, const set<size_t>& proj) const;
    void get_solution_map(map<uint32_t, VarMap>& ret) const;
    bool varRepresentsMonomial(const uint32_t var) const;
    BooleMonomial getMonomForVar(const uint32_t& var) const;
    /// What a CNF variable stands for: an ANF variable, a monomial, a
    /// partner chunk (a sum of monomials folded into one variable), or a
    /// partial sum introduced when cutting an XOR.
    enum VarKind : uint8_t { kind_var, kind_monom, kind_chunk, kind_cut, kind_lineral };
    VarKind getVarKind(const uint32_t var) const { return varKind[var]; }
    const BoolePolynomial& getPolyForVar(const uint32_t var) const { return revCombinedMap[var]; }
    uint32_t getVarForMonom(const BooleMonomial& mono) const;
    size_t getNumClauses() const;
    size_t getAddedAsCNF() const;
    size_t getAddedAsANF() const;
    size_t getAddedAsSimpleANF() const;
    size_t getAddedAsComplexANF() const;
    const vector<pair<vector<Clause>, BoolePolynomial> >& getClauses() const;
    vector<Clause> get_clauses_simple() const;
    /// native XOR clauses (only when config.xorClauses): XOR(vars) = rhs
    typedef pair<vector<uint32_t>, bool> XorClause;
    const vector<XorClause>& getXorClauses() const { return xor_clauses; }
    uint32_t getNumVars() const;
    size_t getNumCutVars() const { return numCutVars; }
    uint64_t getNumAllLits() const;
    uint64_t getNumAllClauses() const;

    friend std::ostream& operator<<(std::ostream& os, const CNF& cnf);
    void print_without_header(std::ostream& os) const;

   private:
    void init();
    void addBoolePolynomial(const BoolePolynomial& eq,
                            const vector<Lineral>* factors = nullptr);
    void addTrivialEquations();
    bool tryAddingPolyWithKarn(const BoolePolynomial& eq,
                               vector<Clause>& setOfClauses) const;

    //Main adders
    uint32_t addBooleMonomial(const BooleMonomial& m);
    uint32_t addChunk(const vector<VarVec>& cover);
    void partnerCover(const BoolePolynomial& poly,
                      vector<vector<VarVec> >& chunks,
                      vector<BooleMonomial>& singles) const;
    uint32_t newVar(VarKind kind, const BoolePolynomial& meaning);
    void addClusters();

    //XOR of CNF variables == rhs: a native xor clause, or cut into pieces
    //of at most cutNum
    void addXor(const vector<uint32_t>& vars, bool rhs,
                vector<Clause>& setOfClauses);
    void addXorWithCuts(const vector<uint32_t>& vars, bool rhs,
                        vector<Clause>& setOfClauses);
    //a polynomial that is a product of linear factors
    bool tryAddingAsProduct(const BoolePolynomial& poly, const vector<Lineral>* factors,
                        vector<Clause>& setOfClauses);
    uint32_t lineralVar(const vector<uint32_t>& vars);
    uint32_t hammingWeight(uint64_t num) const;
    void addEveryCombination(vector<uint32_t>& vars, bool isTrue,
                             vector<Clause>& thisClauses) const;

    //Setup
    const ANF& anf;
    const ConfigData& config;

    //The cumulated CNF data
    vector<pair<vector<Clause>, BoolePolynomial> > clauses;
    ANF::eqs_hash_t in_clauses;
    std::unordered_set<VarVec, VarVecHash> in_products;

    //uint32_t maps -- internal/external mapping of variables/monomial/polynomials
    std::unordered_map<BooleMonomial::hash_type, uint32_t>
        monomMap; // map: outside monom -> inside var
    vector<BoolePolynomial>
        revCombinedMap; // map: inside var -> the polynomial it stands for (a variable, a monomial, a partner chunk, or the partial sum of a cut XOR)
    vector<VarKind> varKind;
    std::unordered_set<VarVec, VarVecHash> monomVarsVV; // monomials that have a CNF var
    std::unordered_map<VarVec, uint32_t, VarVecHash>
        chunkMap; // a partner chunk (its terms, separated by UINT32_MAX) -> inside var
    std::unordered_map<VarVec, uint32_t, VarVecHash>
        lineralMap; // a lineral (its variables) -> the inside var equal to its XOR
    vector<XorClause> xor_clauses;
    static VarVec chunkKey(const vector<VarVec>& cover);
    uint32_t next_cnf_var = 0; ///<CNF variable counter

    //stats
    size_t addedAsANF = 0;
    size_t addedAsSimpleANF = 0;
    size_t addedAsComplexANF = 0;
    size_t addedAsCNF = 0;
    size_t numMonomVars = 0;
    size_t numChunkVars = 0;
    size_t numChunkTerms = 0; // monomials absorbed into chunks
    size_t numCutVars = 0;
    size_t numLineralVars = 0;
    size_t addedAsProduct = 0;
};

inline void CNF::print_without_header(std::ostream& os) const
{
    for (const auto& set_of_cls: clauses) {
        os << set_of_cls.first;
        if (config.writecomments) {
            os << "c " << set_of_cls.second << '\n';
            os << "c ------------\n";
        }
    }
    // CryptoMiniSat's xor clause syntax: "x a b c 0" means a ^ b ^ c = 1,
    // and negating a literal negates the sum
    for (const XorClause& x : xor_clauses) {
        os << "x";
        for (size_t i = 0; i < x.first.size(); i++) {
            os << ' ' << ((i == 0 && !x.second) ? "-" : "") << (x.first[i] + 1);
        }
        os << " 0\n";
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
    return varKind[var] == kind_var || varKind[var] == kind_monom;
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
    cout << "c [cnf-stats] vars " << getNumVars() << " clauses "
         << getNumAllClauses() << " lits " << getNumAllLits()
         << " anf_vars " << anf.getRing().nVariables()
         << " monom_vars " << numMonomVars << " chunk_vars " << numChunkVars
         << " chunk_terms " << numChunkTerms << " cut_vars " << numCutVars
         << " linfactor_vars " << numLineralVars << " product_cls " << addedAsProduct
         << " xor_cls " << xor_clauses.size() << endl;
}

}
