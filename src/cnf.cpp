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
#include <algorithm>
#include <iostream>
#include <iterator>
#include <ostream>
#include <unordered_set>

#include "dimacscache.hpp"
#include "anfcnfutils.hpp"

using namespace BLib;

CNF::CNF(const ANF& _anf, const ConfigData& _config)
    : anf(_anf), config(_config)
{
    init();
    addTrivialEquations();

    // Add regular equations
    const vector<BoolePolynomial>& eqs = anf.getEqs();
    for (const BoolePolynomial& poly : eqs) {
        addBoolePolynomial(poly);
    }
}

CNF::CNF(const char* fname,
         const ANF& _anf,
         const vector<Clause>& clauses_needed_for_anf_import,
         const ConfigData& _config)
    : anf(_anf), config(_config)
{
    init();
    addTrivialEquations();

    vector<Clause> setOfClauses;
    if (fname) {
        // add original CNF clauses
        DIMACSCache dimacs_cache(fname);
        BoolePolynomial eq(0, anf.getRing());
        clauses.push_back(std::make_pair(dimacs_cache.getClauses(), eq));
    }

    //Add clauses needed to cut the large clauses into smaller ones
    //    so as to import into ANF
    if (!clauses_needed_for_anf_import.empty()) {
        BoolePolynomial eq(0, anf.getRing());
        clauses.push_back(std::make_pair(clauses_needed_for_anf_import, eq));
    }

}

size_t CNF::update()
{
    size_t prev = clauses.size();

    // Add new replaced and set variables to CNF
    addTrivialEquations();

    return prev;
}

uint32_t CNF::newVar(VarKind kind, const BoolePolynomial& meaning)
{
    const uint32_t var = next_cnf_var++;
    assert(revCombinedMap.size() == var);
    revCombinedMap.push_back(meaning);
    varKind.push_back(kind);
    return var;
}

void CNF::init()
{
    monomMap.reserve(anf.getRing().nVariables());
    assert(next_cnf_var == 0);
    for (uint32_t var = 0; var < anf.getRing().nVariables();
         ++var) { // Add ALL variables
        BooleVariable v = anf.getRing().variable(var);
        monomMap[v.hash()] = newVar(kind_var, BoolePolynomial(v));
    }

    // If ANF is not OK, then add polynomial '1'
    if (!anf.getOK()) {
        addBoolePolynomial(BoolePolynomial(true, anf.getRing()));
        if (config.verbosity >= 1) {
            cout << "c [CNF-out] added UNSAT to CNF" << endl;
        }
    }
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
    if (config.verbosity >= 1) {
        std::cout << "c [CNF-gen] Number of value assignments = " << nv
                  << "\nc Number of equiv assigments = " << nr << std::endl;
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
        return;
    }

    if (poly.isZero()) {
        return;
    }

    vector<Clause> setOfClauses;
    if (poly.deg() > 1 && poly.nUsedVariables() <= config.brickestein_algo_cutoff &&
        BrickesteinAlgo32(poly, setOfClauses)) {
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

        // Linearise: every nonlinear part of the polynomial becomes one CNF
        // variable. With the partner strategies a variable may stand for
        // several terms at once (x*y + x, x*y + x*z, ...); otherwise for one
        // monomial each (the "standard strategy").
        vector<BoolePolynomial> chunks;
        vector<BooleMonomial> singles;
        if (config.doPartner && poly.deg() >= 2) {
            partnerCover(poly, chunks, singles);
        } else {
            for (const BooleMonomial& m : poly) {
                if (!m.isConstant()) singles.push_back(m);
            }
        }
        bool rhs = poly.hasConstantPart();
        vector<uint32_t> xor_vars;
        for (const BoolePolynomial& g : chunks) {
            if (g.hasConstantPart()) rhs ^= true; // the chunk absorbed the +1
            xor_vars.push_back(addChunk(g));
        }
        for (const BooleMonomial& m : singles) {
            xor_vars.push_back(addBooleMonomial(m));
        }
        addXorWithCuts(xor_vars, rhs, setOfClauses);
    }

    clauses.push_back(make_pair(setOfClauses, poly));
}

///////////////////////////////////////////////////////////////////////////////
// Partner strategies, after P. Jovanovic and M. Kreuzer, "Algebraic Attacks
// using SAT-Solvers", Groups Complexity Cryptology 2 (2010).
//
// The standard strategy gives every nonlinear monomial its own CNF variable.
// The paper instead substitutes small *combinations* of terms by one
// variable: x*y + x (linear partner), x*y + x + y + 1 (double partner),
// x*y + x*z (quadratic partner) and x*y*z + x*y*w (cubic partner). All of
// these are of the form P * h(F): a common monomial P times a polynomial h
// in a few "free" variables F, and y <-> P & h(F) has a short CNF. Here the
// cover is searched for generally: for every monomial m of the polynomial,
// every split of m into P and F (|F| <= 3), optionally with one extra free
// variable w taken from a term P*S*w that is present, and the split that
// absorbs the most terms P*S (S subset of F) wins. This finds all four
// strategies of the paper, their mixtures (x*y + x*z + x = x*(y+z+1)), and
// generalisations like x*y*z + x*y + x*z + x = x*(y+1)*(z+1).
///////////////////////////////////////////////////////////////////////////////

namespace {

typedef vector<uint32_t> VarVec; // a monomial as its sorted variable indices

struct VarVecHash {
    size_t operator()(const VarVec& v) const
    {
        size_t h = 1469598103934665603ULL;
        for (const uint32_t x : v) {
            h ^= x + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        }
        return h;
    }
};

// P union S, both sorted
VarVec merge_vars(const VarVec& a, const VarVec& b)
{
    VarVec out;
    out.reserve(a.size() + b.size());
    std::set_union(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(out));
    return out;
}

unsigned popcount(unsigned x)
{
    unsigned c = 0;
    for (; x; x >>= 1) c += x & 1;
    return c;
}

}

void CNF::partnerCover(const BoolePolynomial& poly,
                       vector<BoolePolynomial>& chunks,
                       vector<BooleMonomial>& singles) const
{
    const BoolePolyRing& ring = anf.getRing();
    std::unordered_set<VarVec, VarVecHash> rem;
    vector<VarVec> terms;
    for (const BooleMonomial& m : poly) {
        VarVec v(m.begin(), m.end());
        rem.insert(v);
        terms.push_back(v);
    }
    // children[P] = every w such that P*w is a term: the partners of P
    std::unordered_map<VarVec, vector<uint32_t>, VarVecHash> children;
    for (const VarVec& t : terms) {
        for (size_t i = 0; i < t.size(); i++) {
            VarVec parent(t);
            parent.erase(parent.begin() + i);
            children[parent].push_back(t[i]);
        }
    }
    // anchors of highest degree first
    std::stable_sort(terms.begin(), terms.end(),
                     [](const VarVec& a, const VarVec& b) { return a.size() > b.size(); });

    const unsigned max_free = 3;
    for (const VarVec& m : terms) {
        if (m.size() < 2) break;
        if (rem.find(m) == rem.end()) continue; // already absorbed

        vector<VarVec> best_cover;
        long best_score = 0;
        unsigned best_free = 0;
        auto consider = [&](const VarVec& P, const VarVec& F) {
            // the terms P*S for S subset of F that are present
            vector<VarVec> cover;
            BoolePolynomial g(ring);
            size_t fresh_monoms = 0; // nonlinear terms that have no CNF var yet
            for (unsigned smask = 0; smask < (1u << F.size()); smask++) {
                VarVec S;
                for (size_t i = 0; i < F.size(); i++) {
                    if (smask & (1u << i)) S.push_back(F[i]);
                }
                VarVec t = merge_vars(P, S);
                if (rem.find(t) == rem.end()) continue;
                cover.push_back(t);
                BooleMonomial mono(ring);
                for (const uint32_t v : t) mono *= ring.variable(v);
                g += mono;
                if (t.size() >= 2 && monomMap.find(mono.hash()) == monomMap.end()) {
                    fresh_monoms++;
                }
            }
            if (cover.size() < 2) return;
            // Estimated CNF variables saved compared to the standard
            // strategy, in units of 1/(cutNum-1): every absorbed term is one
            // XOR argument less (an argument costs 1/(cutNum-1) cutting
            // variables), every fresh nonlinear term is one monomial variable
            // less, and the chunk itself costs one variable unless another
            // polynomial already introduced it. A monomial that already has
            // a variable (shared with another polynomial) is thus only
            // absorbed when that clearly pays off.
            const long slot = config.cutNum - 1;
            const bool exists = chunkMap.find(g.stableHash()) != chunkMap.end();
            const long score = (long)(cover.size() - 1)
                + slot * ((long)fresh_monoms - (exists ? 0 : 1));
            if (score <= 0) return;
            if (score > best_score ||
                (score == best_score && F.size() < best_free)) {
                best_cover.swap(cover);
                best_score = score;
                best_free = F.size();
            }
        };

        const unsigned d = m.size();
        for (unsigned mask = 1; mask < (1u << d); mask++) {
            if (popcount(mask) > max_free) continue;
            VarVec F0, P;
            for (unsigned i = 0; i < d; i++) {
                if (mask & (1u << i)) F0.push_back(m[i]);
                else P.push_back(m[i]);
            }
            consider(P, F0);

            // one extra free variable w, not in m: P*S*w must be a term
            if (F0.size() >= max_free) continue;
            vector<uint32_t> cands;
            for (unsigned smask = 0; smask < (1u << F0.size()); smask++) {
                VarVec S;
                for (size_t i = 0; i < F0.size(); i++) {
                    if (smask & (1u << i)) S.push_back(F0[i]);
                }
                auto it = children.find(merge_vars(P, S));
                if (it == children.end()) continue;
                for (const uint32_t w : it->second) {
                    if (std::binary_search(m.begin(), m.end(), w)) continue;
                    if (std::find(cands.begin(), cands.end(), w) == cands.end()) {
                        cands.push_back(w);
                    }
                }
                if (cands.size() > 64) break;
            }
            for (const uint32_t w : cands) {
                VarVec F = merge_vars(F0, VarVec(1, w));
                consider(P, F);
            }
        }

        if (best_cover.size() < 2) continue; // no partner: standard strategy
        BoolePolynomial g(ring);
        for (const VarVec& t : best_cover) {
            BooleMonomial mono(ring);
            for (const uint32_t v : t) mono *= ring.variable(v);
            g += mono;
            rem.erase(t);
        }
        chunks.push_back(g);
    }

    for (const VarVec& t : terms) {
        if (t.empty()) continue; // the constant stays in the XOR's rhs
        if (rem.find(t) == rem.end()) continue;
        BooleMonomial mono(ring);
        for (const uint32_t v : t) mono *= ring.variable(v);
        singles.push_back(mono);
    }
}

namespace {

// Small clause-set minimisation for the definition of a chunk variable:
// merge clauses that differ only in the sign of one literal, then drop
// duplicates and subsumed clauses. Clause literals are kept sorted.
void minimise_clauses(vector<vector<Lit> >& cls)
{
    for (auto& c : cls) std::sort(c.begin(), c.end());
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < cls.size() && !changed; i++) {
            for (size_t j = i + 1; j < cls.size() && !changed; j++) {
                if (cls[i].size() != cls[j].size()) continue;
                size_t diff = 0, at = 0;
                for (size_t k = 0; k < cls[i].size(); k++) {
                    if (cls[i][k] == cls[j][k]) continue;
                    if (cls[i][k].var() == cls[j][k].var()) {
                        diff++;
                        at = k;
                    } else {
                        diff = 2;
                        break;
                    }
                }
                if (diff != 1) continue;
                vector<Lit> merged(cls[i]);
                merged.erase(merged.begin() + at);
                cls.push_back(merged);
                changed = true;
            }
        }
        // drop duplicates and subsumed clauses
        vector<vector<Lit> > kept;
        for (size_t i = 0; i < cls.size(); i++) {
            bool drop = false;
            for (size_t j = 0; j < cls.size() && !drop; j++) {
                if (i == j) continue;
                if (cls[j].size() > cls[i].size()) continue;
                if (cls[j].size() == cls[i].size() && j > i) continue; // equal: keep first
                if (std::includes(cls[i].begin(), cls[i].end(),
                                  cls[j].begin(), cls[j].end())) {
                    drop = true;
                }
            }
            if (!drop) kept.push_back(cls[i]);
        }
        cls.swap(kept);
    }
}

}

uint32_t CNF::addChunk(const BoolePolynomial& g)
{
    const auto it = chunkMap.find(g.stableHash());
    if (it != chunkMap.end()) {
        return it->second;
    }

    // g = P * h(F): P is the gcd of all terms, F the remaining variables
    BooleMonomial P(anf.getRing());
    bool first = true;
    for (const BooleMonomial& m : g) {
        if (first) {
            P = m;
            first = false;
        } else {
            P = P.GCD(m);
        }
    }
    const BoolePolynomial h = g / P;
    VarVec F;
    for (const uint32_t v : h.usedVariables()) F.push_back(v);
    assert(F.size() <= 16);

    const uint32_t y = newVar(kind_chunk, g);
    chunkMap[g.stableHash()] = y;
    numChunkVars++;
    numChunkTerms += g.length();

    std::vector<uint32_t> Pvars;
    for (const uint32_t v : P) Pvars.push_back(monomMap.find(BooleVariable(v, anf.getRing()).hash())->second);
    std::vector<uint32_t> Fvars;
    for (const uint32_t v : F) Fvars.push_back(monomMap.find(BooleVariable(v, anf.getRing()).hash())->second);

    // y <-> (all of P) & h(F)
    vector<vector<Lit> > cls;
    for (const uint32_t p : Pvars) {
        cls.push_back({Lit(y, true), Lit(p, false)});
    }
    for (uint32_t alpha = 0; alpha < (1u << F.size()); alpha++) {
        // evaluate h at F = alpha
        bool val = false;
        for (const BooleMonomial& m : h) {
            bool term = true;
            for (const uint32_t v : m) {
                const size_t i = std::lower_bound(F.begin(), F.end(), v) - F.begin();
                term &= (alpha >> i) & 1;
            }
            val ^= term;
        }
        // the clause falsified exactly by the assignment alpha to F
        vector<Lit> c;
        for (size_t i = 0; i < F.size(); i++) {
            c.push_back(Lit(Fvars[i], (alpha >> i) & 1));
        }
        if (!val) {
            c.push_back(Lit(y, true)); // y -> not alpha
        } else {
            c.push_back(Lit(y, false)); // P & alpha -> y
            for (const uint32_t p : Pvars) c.push_back(Lit(p, true));
        }
        cls.push_back(c);
    }
    minimise_clauses(cls);

    vector<Clause> setOfClauses;
    for (const auto& c : cls) setOfClauses.push_back(Clause(c));
    clauses.push_back(std::make_pair(setOfClauses, g)); // g is what y stands for
    return y;
}

void CNF::addXorWithCuts(const vector<uint32_t>& vars, bool rhs,
                         vector<Clause>& setOfClauses)
{
    assert(config.cutNum > 1);
    if (vars.empty()) {
        if (rhs) setOfClauses.push_back(Clause(vector<Lit>())); // 1 = 0
        return;
    }

    size_t pos = 0;
    bool have_carry = false;
    uint32_t carry = 0;
    BoolePolynomial upto(getANFRing()); // what the carry variable stands for
    while (true) {
        vector<uint32_t> cur;
        if (have_carry) cur.push_back(carry);
        while (pos < vars.size()) {
            // once at the cutting number, only take one more variable if it
            // is the last one: cheaper than a cut plus a 2-variable XOR
            if (cur.size() >= config.cutNum && vars.size() - pos != 1) break;
            upto += revCombinedMap[vars[pos]];
            cur.push_back(vars[pos++]);
        }
        if (pos == vars.size()) {
            addEveryCombination(cur, rhs, setOfClauses);
            return;
        }
        // cut: a new variable equal to the XOR so far
        carry = newVar(kind_cut, upto);
        numCutVars++;
        have_carry = true;
        cur.push_back(carry);
        addEveryCombination(cur, false, setOfClauses);
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
    const uint32_t newVar = this->newVar(kind_monom, BoolePolynomial(m));
    monomMap[m.hash()] = newVar;
    numMonomVars++;

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

void CNF::get_solution_map(map<uint32_t, VarMap>& ret) const
{
    for (size_t i = 0; i < getNumVars(); ++i) {
        // only map monomials which are single variables
        if (varRepresentsMonomial(i)) {
            const BooleMonomial& m(revCombinedMap[i].lead());

            //Only single-vars
            if (m.deg() == 1) {
                const uint32_t var = m.firstVariable().index();
                VarMap m;
                m.inv = false;
                m.other_var = i;
                m.type = Bosph::VarMap::cnf_var;
                ret[var] = m;
            }
        }
    }
}

void CNF::print_solution_map(std::ofstream* ofs)
{
    for (size_t i = 0; i < getNumVars(); ++i) {
        // only map monomials which are single variables
        if (varRepresentsMonomial(i)) {
            const BooleMonomial& m(revCombinedMap[i].lead());

            //Only single-vars
            if (m.deg() == 1) {
                const uint32_t var = m.firstVariable().index();
                *ofs << "Internal-ANF-var " << var << " = solution-var " << i << endl;
            }
        }
    }
}


void CNF::write_projection_set(std::ofstream* ofs, const set<size_t>& proj) const
{
    *ofs << "c p show ";
    for(const auto i: proj) {
        // only map monomials which are single variables
        if (varRepresentsMonomial(i)) {
            const BooleMonomial& m(revCombinedMap[i].lead());

            //Only single-vars
            if (m.deg() == 1) {
                *ofs << i+1 << " ";
            }
        }
    }
    *ofs << "0" << endl;
}

BooleMonomial CNF::getMonomForVar(const uint32_t& var) const
{
    if (varRepresentsMonomial(var))
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

vector<Clause> CNF::get_clauses_simple() const
{
    vector<Clause> ret;
    for(auto& cls: clauses) {
        for(auto& cl: cls.first) {
            ret.push_back(cl);
        }
    }
    return ret;
}
