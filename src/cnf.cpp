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
#include "linfactor.hpp"

using namespace BLib;

CNF::CNF(const ANF& _anf, const ConfigData& _config)
    : anf(_anf), config(_config)
{
    init();
    addTrivialEquations();
    if (config.karnCluster > 0) addClusters();

    // Add regular equations
    for (size_t i = 0; i < anf.size(); i++) {
        if (anf.isProduct(i)) {
            addBoolePolynomial(BoolePolynomial(anf.getRing()), &anf.getFactors(i));
        } else {
            addBoolePolynomial(anf.eq(i), nullptr);
        }
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

///////////////////////////////////////////////////////////////////////////////
// Joint encoding of small nonlinear equations that share variables
// (e.g. the five output equations of an S-box): the forbidden assignments
// of all equations of a cluster together are covered by one clause set over
// the union of their variables, instead of one clause set per equation.
// The clauses then propagate across the equations of the cluster.
///////////////////////////////////////////////////////////////////////////////

void CNF::addClusters()
{
    const uint32_t max_vars = config.karnCluster;
    vector<size_t> cand; // equations that would be encoded by Brickenstein
    vector<vector<uint32_t> > vars_of;
    vector<Lineral> f;
    vector<VarVec> qls;
    VarVec qrest;
    bool qc;
    for (size_t i = 0; i < anf.size(); i++) {
        if (anf.isProduct(i) || anf.degOf(i) < 2) continue;
        const BoolePolynomial& p = anf.eq(i);
        if (p.nUsedVariables() > config.brickestein_algo_cutoff || p.nUsedVariables() > max_vars) continue;
        if (config.doFactor && factor_into_linerals(p, f) && f.size() >= 2) continue;
        if (config.quadSplit > 0 && quadSplitApplies(p, qls, qrest, qc)) continue;
        cand.push_back(i);
        vector<uint32_t> vs;
        for (const uint32_t v : p.usedVariables()) vs.push_back(v);
        vars_of.push_back(vs);
    }
    if (cand.size() < 2) return;

    vector<vector<size_t> > occ(anf.getRing().nVariables()); // var -> candidate positions
    for (size_t c = 0; c < cand.size(); c++) {
        for (const uint32_t v : vars_of[c]) occ[v].push_back(c);
    }

    vector<char> taken(cand.size(), 0);
    vector<uint32_t> in_cluster(anf.getRing().nVariables(), 0); // var -> cluster stamp
    uint32_t stamp = 0;
    size_t num_clusters = 0, num_members = 0;
    for (size_t c0 = 0; c0 < cand.size(); c0++) {
        if (taken[c0]) continue;
        stamp++;
        vector<size_t> members(1, c0);
        vector<uint32_t> cvars = vars_of[c0];
        for (const uint32_t v : cvars) in_cluster[v] = stamp;
        taken[c0] = 1;
        while (true) {
            // the untaken candidate sharing the most variables with the cluster
            size_t best = cand.size();
            size_t best_shared = 1; // at least 2 shared variables
            size_t best_new = 0;
            for (const uint32_t v : cvars) {
                for (const size_t c : occ[v]) {
                    if (taken[c]) continue;
                    size_t shared = 0;
                    for (const uint32_t w : vars_of[c]) shared += (in_cluster[w] == stamp);
                    const size_t fresh = vars_of[c].size() - shared;
                    if (cvars.size() + fresh > max_vars) continue;
                    if (shared > best_shared || (shared == best_shared && best != cand.size() && fresh < best_new)) {
                        best = c;
                        best_shared = shared;
                        best_new = fresh;
                    }
                }
            }
            if (best == cand.size()) break;
            taken[best] = 1;
            members.push_back(best);
            for (const uint32_t w : vars_of[best]) {
                if (in_cluster[w] != stamp) {
                    in_cluster[w] = stamp;
                    cvars.push_back(w);
                }
            }
        }
        if (members.size() < 2) {
            taken[c0] = 0; // encoded on its own later
            continue;
        }

        std::sort(cvars.begin(), cvars.end());
        const vector<polybori::CCuddNavigator::value_type> vidx(cvars.begin(), cvars.end());
        BooleSet ones(anf.getRing());
        BoolePolynomial all_zero(true, anf.getRing()); // prod (1 + p_i)
        for (const size_t c : members) {
            const BoolePolynomial& p = anf.eq(cand[c]);
            in_clauses.insert(p.hash());
            ones = ones.unite(BrickensteinOnes(p, vidx));
            all_zero *= p + BooleConstant(true);
        }
        vector<Clause> setOfClauses;
        BrickensteinCover(anf.getRing(), ones, vidx, setOfClauses);
        clauses.push_back(make_pair(setOfClauses, all_zero + BooleConstant(true)));
        addedAsCNF += members.size();
        num_clusters++;
        num_members += members.size();
    }
    if (config.verbosity >= 1) {
        cout << "c [karn-cluster] " << num_members << " equations in " << num_clusters
             << " clusters of at most " << max_vars << " variables" << endl;
    }
}

void CNF::addBoolePolynomial(const BoolePolynomial& poly,
                             const vector<Lineral>* factors)
{
    // a product of linear factors: no polynomial is needed
    if (factors != nullptr && factors->size() >= 2 && config.doFactor) {
        if (!in_products.insert(product_key(*factors)).second) return;
        vector<Clause> setOfClauses;
        const bool ok = tryAddingAsProduct(poly, factors, setOfClauses);
        assert(ok);
        (void)ok;
        addedAsProduct++;
        clauses.push_back(make_pair(setOfClauses,
            config.writecomments ? expand_linerals(anf.getRing(), *factors)
                                 : BoolePolynomial(anf.getRing())));
        return;
    }
    if (factors != nullptr && factors->size() >= 2 && poly.isZero()) {
        // product encoding is off: work on the expanded polynomial
        addBoolePolynomial(expand_linerals(anf.getRing(), *factors), nullptr);
        return;
    }

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
    if (config.doFactor && poly.deg() > 1 && tryAddingAsProduct(poly, factors, setOfClauses)) {
        addedAsProduct++;
    } else if (config.quadSplit > 0 && poly.deg() == 2 && tryAddingAsQuadForm(poly, setOfClauses)) {
        addedAsQuad++;
    } else if (poly.deg() > 1 && poly.nUsedVariables() <= config.brickestein_algo_cutoff &&
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
        vector<vector<VarVec> > chunks;
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
        for (const vector<VarVec>& cover : chunks) {
            for (const VarVec& t : cover) {
                if (t.empty()) rhs ^= true; // the chunk absorbed the +1
            }
            xor_vars.push_back(addChunk(cover));
        }
        for (const BooleMonomial& m : singles) {
            xor_vars.push_back(addBooleMonomial(m));
        }
        addXor(xor_vars, rhs, setOfClauses);
    }

    clauses.push_back(make_pair(setOfClauses, poly));
}

///////////////////////////////////////////////////////////////////////////////
// A polynomial that is a product of linear factors (l_1 + c_1)...(l_k + c_k)
// is 0 exactly when some factor is: it is the clause "l_1 = c_1 or ... or
// l_k = c_k". Every factor with more than one variable gets one CNF variable
// y equal to its XOR, shared between all polynomials that contain the same
// factor, and the clause is written over those. Compared to one variable per
// monomial (a product of three 50-variable factors has 125000 monomials)
// this is a tiny encoding, and the XORs stay whole.
///////////////////////////////////////////////////////////////////////////////

bool CNF::tryAddingAsProduct(const BoolePolynomial& poly, const vector<Lineral>* known,
                         vector<Clause>& setOfClauses)
{
    vector<Lineral> factors;
    if (known != nullptr && known->size() >= 2) {
        factors = *known; // maintained by the ANF, may share variables
    } else if (!factor_into_linerals(poly, factors) || factors.size() < 2) {
        return false;
    }

    vector<Lit> lits;
    for (const Lineral& f : factors) {
        // literal "l = c": for a single variable v that is v (c = 1) or -v
        if (f.vars.size() == 1) {
            const uint32_t v = monomMap.find(BooleVariable(f.vars[0], anf.getRing()).hash())->second;
            lits.push_back(Lit(v, !f.c));
        } else {
            lits.push_back(Lit(lineralVar(f.vars), !f.c));
        }
    }
    // with shared variables two factors can be the same linear form
    std::sort(lits.begin(), lits.end());
    lits.erase(std::unique(lits.begin(), lits.end()), lits.end());
    for (size_t i = 0; i + 1 < lits.size(); i++) {
        if (lits[i].var() == lits[i + 1].var()) return true; // tautology: no clause
    }
    setOfClauses.push_back(Clause(lits));
    return true;
}

///////////////////////////////////////////////////////////////////////////////
// quad-split: a quadratic polynomial as its Dickson decomposition
//   l_1 * l_2 + l_3 * l_4 + ... + rest = 0
// (linfactor.cpp). Every linear form with more than one variable gets the
// XOR-defined CNF variable that products of linear factors use, shared
// between all equations that contain the same form; every product gets a
// variable y = y1 & y2 (three clauses), shared as well; and the equation is
// the XOR of the product variables and the rest. The S-box equations of a
// cipher have one or two products each, and the raw CNFs of such systems
// are written with exactly these auxiliary variables (one per linear form);
// against the joint clause set of a cluster, the solver's learnt clauses can
// then speak about the linear forms.
///////////////////////////////////////////////////////////////////////////////

bool CNF::quadSplitApplies(const BoolePolynomial& poly, vector<VarVec>& linerals,
                           VarVec& rest, bool& c) const
{
    if (config.quadSplit == 0 || poly.deg() != 2) return false;
    size_t nquad = 0;
    for (const BooleMonomial& m : poly) nquad += (m.deg() == 2);
    if (nquad < config.quadSplitMin) return false;
    if (!quad_form_decompose(poly, linerals, rest, c)) return false;
    return linerals.size() / 2 <= config.quadSplit;
}

uint32_t CNF::andVar(uint32_t y1, uint32_t y2, const BoolePolynomial& meaning)
{
    if (y1 > y2) std::swap(y1, y2);
    const uint64_t key = ((uint64_t)y1 << 32) | y2;
    const auto it = andMap.find(key);
    if (it != andMap.end()) return it->second;
    const uint32_t y = newVar(kind_chunk, meaning);
    andMap[key] = y;
    numAndVars++;
    // y <-> y1 & y2
    vector<Clause> setOfClauses;
    setOfClauses.push_back(Clause(vector<Lit>{Lit(y, true), Lit(y1, false)}));
    setOfClauses.push_back(Clause(vector<Lit>{Lit(y, true), Lit(y2, false)}));
    setOfClauses.push_back(Clause(vector<Lit>{Lit(y, false), Lit(y1, true), Lit(y2, true)}));
    clauses.push_back(std::make_pair(setOfClauses, meaning));
    return y;
}

bool CNF::tryAddingAsQuadForm(const BoolePolynomial& poly, vector<Clause>& setOfClauses)
{
    vector<VarVec> linerals;
    VarVec rest;
    bool c;
    if (!quadSplitApplies(poly, linerals, rest, c)) return false;
    const BoolePolyRing& ring = anf.getRing();
    auto var_of = [&](const VarVec& l) {
        if (l.size() == 1) return monomMap.find(BooleVariable(l[0], ring).hash())->second;
        return lineralVar(l);
    };
    vector<uint32_t> xor_vars;
    for (size_t k = 0; k + 1 < linerals.size(); k += 2) {
        BoolePolynomial f(ring), g(ring);
        for (const uint32_t v : linerals[k]) f += BooleVariable(v, ring);
        for (const uint32_t v : linerals[k + 1]) g += BooleVariable(v, ring);
        xor_vars.push_back(andVar(var_of(linerals[k]), var_of(linerals[k + 1]), f * g));
    }
    for (const uint32_t v : rest) xor_vars.push_back(monomMap.find(BooleVariable(v, ring).hash())->second);
    addXor(xor_vars, c, setOfClauses);
    return true;
}

uint32_t CNF::lineralVar(const vector<uint32_t>& anf_vars)
{
    const auto it = lineralMap.find(anf_vars);
    if (it != lineralMap.end()) return it->second;

    // the polynomial the new variable stands for, summed pairwise (adding
    // 50 variables one by one to a growing polynomial is quadratic)
    vector<BoolePolynomial> level;
    vector<uint32_t> cnf_vars;
    for (const uint32_t v : anf_vars) {
        level.push_back(BoolePolynomial(BooleVariable(v, anf.getRing())));
        cnf_vars.push_back(monomMap.find(BooleVariable(v, anf.getRing()).hash())->second);
    }
    while (level.size() > 1) {
        vector<BoolePolynomial> next;
        for (size_t k = 0; k + 1 < level.size(); k += 2) next.push_back(level[k] + level[k + 1]);
        if (level.size() % 2) next.push_back(level.back());
        level.swap(next);
    }
    BoolePolynomial l(anf.getRing());
    if (!level.empty()) l = level.front();
    const uint32_t y = newVar(kind_lineral, l);
    lineralMap[anf_vars] = y;
    numLineralVars++;

    // y + l = 0
    vector<Clause> setOfClauses;
    cnf_vars.push_back(y);
    addXor(cnf_vars, false, setOfClauses);
    clauses.push_back(std::make_pair(setOfClauses, l)); // l is what y stands for
    return y;
}

void CNF::addXor(const vector<uint32_t>& vars, bool rhs,
                 vector<Clause>& setOfClauses)
{
    if (!config.xorClauses || vars.size() <= 1) {
        addXorWithCuts(vars, rhs, setOfClauses);
        return;
    }
    if (config.xorMaxLen < 3 || vars.size() <= config.xorMaxLen) {
        xor_clauses.push_back(std::make_pair(vars, rhs));
        return;
    }
    // long XOR: chain of native pieces of at most xorMaxLen variables,
    // linked by fresh variables (each piece XORs to 0 except the last)
    size_t pos = 0;
    bool have_carry = false;
    uint32_t carry = 0;
    BoolePolynomial upto(getANFRing());
    while (true) {
        vector<uint32_t> cur;
        if (have_carry) cur.push_back(carry);
        while (pos < vars.size()) {
            if (cur.size() >= config.xorMaxLen - 1 && vars.size() - pos != 1) break;
            if (config.writecomments) upto += revCombinedMap[vars[pos]];
            cur.push_back(vars[pos++]);
        }
        if (pos == vars.size()) {
            xor_clauses.push_back(std::make_pair(cur, rhs));
            return;
        }
        carry = newVar(kind_cut, upto);
        numCutVars++;
        have_carry = true;
        cur.push_back(carry);
        xor_clauses.push_back(std::make_pair(cur, false));
    }
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

typedef CNF::VarVec VarVec;

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

CNF::VarVec CNF::chunkKey(const vector<VarVec>& cover)
{
    VarVec key;
    for (const VarVec& t : cover) {
        key.insert(key.end(), t.begin(), t.end());
        key.push_back(std::numeric_limits<uint32_t>::max());
    }
    return key;
}

void CNF::partnerCover(const BoolePolynomial& poly,
                       vector<vector<VarVec> >& chunks,
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
        vector<VarVec> cover;
        auto consider = [&](const VarVec& P, const VarVec& F) {
            // the terms P*S for S subset of F that are present
            cover.clear();
            size_t fresh_monoms = 0; // nonlinear terms that have no CNF var yet
            for (unsigned smask = 0; smask < (1u << F.size()); smask++) {
                VarVec S;
                for (size_t i = 0; i < F.size(); i++) {
                    if (smask & (1u << i)) S.push_back(F[i]);
                }
                VarVec t = merge_vars(P, S);
                if (rem.find(t) == rem.end()) continue;
                if (t.size() >= 2 && monomVarsVV.find(t) == monomVarsVV.end()) {
                    fresh_monoms++;
                }
                cover.push_back(std::move(t));
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
            const bool exists = chunkMap.find(chunkKey(cover)) != chunkMap.end();
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
                    if (cands.size() >= 16) break;
                }
                if (cands.size() >= 16) break;
            }
            for (const uint32_t w : cands) {
                VarVec F = merge_vars(F0, VarVec(1, w));
                consider(P, F);
            }
        }

        if (best_cover.size() < 2) continue; // no partner: standard strategy
        for (const VarVec& t : best_cover) rem.erase(t);
        chunks.push_back(best_cover);
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

uint32_t CNF::addChunk(const vector<VarVec>& cover)
{
    const VarVec key = chunkKey(cover);
    const auto it = chunkMap.find(key);
    if (it != chunkMap.end()) {
        return it->second;
    }

    // g = P * h(F): P is the gcd of all terms, F the remaining variables
    const BoolePolyRing& ring = anf.getRing();
    BoolePolynomial g(ring);
    BooleMonomial P(ring);
    bool first = true;
    for (const VarVec& t : cover) {
        BooleMonomial mono(ring);
        for (const uint32_t v : t) mono *= ring.variable(v);
        g += mono;
        if (first) {
            P = mono;
            first = false;
        } else {
            P = P.GCD(mono);
        }
    }
    const BoolePolynomial h = g / P;
    VarVec F;
    for (const uint32_t v : h.usedVariables()) F.push_back(v);
    assert(F.size() <= 16);

    const uint32_t y = newVar(kind_chunk, g);
    chunkMap[key] = y;
    numChunkVars++;
    numChunkTerms += cover.size();

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
    // what the carry variable stands for; only needed for the CNF comments
    // and expensive to build for polynomials with thousands of terms
    BoolePolynomial upto(getANFRing());
    while (true) {
        vector<uint32_t> cur;
        if (have_carry) cur.push_back(carry);
        while (pos < vars.size()) {
            // once at the cutting number, only take one more variable if it
            // is the last one: cheaper than a cut plus a 2-variable XOR
            if (cur.size() >= config.cutNum && vars.size() - pos != 1) break;
            if (config.writecomments) upto += revCombinedMap[vars[pos]];
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
    monomVarsVV.insert(VarVec(m.begin(), m.end()));
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
    // CNF variable i+1 is ANF variable i for every original variable
    *ofs << "c p show ";
    for (const auto i : proj) {
        if (i < anf.getRing().nVariables()) *ofs << i + 1 << " ";
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
    for (const XorClause& x : xor_clauses) numLits += x.first.size();
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
    uint64_t numClauses = xor_clauses.size();
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
