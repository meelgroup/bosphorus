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

#include "anf.hpp"

#include <cctype>
#include <fstream>
#include <string>
#include <iomanip>

#include "replacer.hpp"
#include "time_mem.h"
#include <algorithm>
#include <iterator>

using std::cout;
using std::endl;
using namespace BLib;

ANF::ANF(const polybori::BoolePolyRing* _ring, ConfigData& _config)
    : ring(_ring),
      config(_config),
      replacer(new Replacer)
{
    //ensure that the variables are not new
    for (size_t i = 0; i < ring->nVariables(); i++) {
        replacer->newVar(i);
    }

    assert(occur.empty());
    occur.resize(ring->nVariables());
}

ANF::~ANF()
{
    if (replacer != nullptr)
        delete replacer;
}

ANFStats ANF::get_stats() const
{
    ANFStats s;
    s.eqs = eqs.size();
    for (size_t i = 0; i < eqs.size(); i++) {
        s.monoms += eq_len[i];
        const int deg = degOf(i);
        if (deg <= 1) s.lin_eqs++;
        else s.nonlin_eqs++;
        if (deg > 0 && (uint64_t)deg > s.max_deg) s.max_deg = deg;
    }
    s.free_vars = replacer->getNumUnknownVars();
    s.set_vars = replacer->getNumSetVars();
    s.repl_vars = replacer->getNumReplacedVars();
    s.mem_mb = memUsed() / (1024ULL * 1024ULL);
    s.time = cpuTime();
    return s;
}

// KMA Chai: Check if this polynomial can cause further ANF propagation
bool ANF::check_if_need_update(const BoolePolynomial& poly,
                               unordered_set<uint32_t>& updatedVars)
{
    //////////////////
    // Assign values
    //////////////////

    // If polynomial is "x = 0" or "x + 1 = 0", set the value of x
    if (poly.nUsedVariables() == 1 && poly.deg() == 1) {
        uint32_t v = poly.usedVariables().firstVariable().index();
        auto updated_vars = replacer->setValue(v, poly.hasConstantPart());

        // Mark updated vars
        for (const uint32_t& var : updated_vars) {
            updatedVars.insert(var);
        }
        return true;
    }

    // If polynomial is "a*b*c*.. + 1 = 0", then all variables must be TRUE
    if (poly.isPair() && poly.hasConstantPart()) {
        for (const uint32_t& var_idx : poly.firstTerm()) {
            auto updated_vars = replacer->setValue(var_idx, true);

            // Mark updated vars
            for (const uint32_t var : updated_vars) {
                updatedVars.insert(var);
            }
        }
        return true;
    }

    //////////////////
    // Assign anti/equivalences
    //////////////////

    // If polynomial is "x + y = 0" or "x + y + 1 = 0", set the value of x in terms of y
    if (poly.nUsedVariables() == 2 && poly.deg() == 1) {
        uint32_t var[2];
        size_t i = 0;
        for (const uint32_t v : poly.usedVariables()) {
            var[i++] = v;
        }

        // Make the update
        vector<uint32_t> ret =
            replacer->setReplace(var[0], Lit(var[1], poly.hasConstantPart()));
        updatedVars.insert(var[0]);
        updatedVars.insert(var[1]);

        // Mark updated vars
        for (const uint32_t& var_idx : ret) {
            updatedVars.insert(var_idx);
        }
        return true;
    }
    return false;
}

bool ANF::addBoolePolynomial(const BoolePolynomial& poly)
{
    // Don't add constants
    if (poly.isConstant()) {
        // Check UNSAT
        if (poly.isOne()) {
            replacer->setNOTOK();
        }
        return false;
    }

    // If poly already present, don't add it
    auto ins = eqs_hash.insert(poly.hash());
    if (!ins.second)
        return false;

    addPolyToOccur(poly, eqs.size());

    eqs.push_back(poly);
    poly_valid.push_back(1);
    factors.push_back(vector<Lineral>());
    eq_len.push_back(poly.length());

    return true;
}

bool ANF::addTerms(vector<VarVec>& terms)
{
    // x*x = x, then x + x = 0: drop pairs of equal terms (in place)
    for (VarVec& t : terms) {
        std::sort(t.begin(), t.end());
        t.erase(std::unique(t.begin(), t.end()), t.end());
    }
    std::sort(terms.begin(), terms.end());
    size_t w = 0;
    for (size_t i = 0; i < terms.size();) {
        size_t j = i;
        while (j < terms.size() && terms[j] == terms[i]) j++;
        if ((j - i) % 2) terms[w++].swap(terms[i]);
        i = j;
    }
    terms.resize(w);

    vector<Lineral> f;
    if (terms.size() >= 4 && factor_terms(terms, f) && f.size() >= 2) {
        const VarVec key = product_key(f);
        if (!prod_keys.insert(key).second) return false; // duplicate
        // occurrence lists straight from the factors (a variable shared by
        // two factors is listed once)
        VarVec used;
        for (const Lineral& l : f) used.insert(used.end(), l.vars.begin(), l.vars.end());
        std::sort(used.begin(), used.end());
        used.erase(std::unique(used.begin(), used.end()), used.end());
        for (const uint32_t v : used) occur[v].push_back(eqs.size());
        eqs.push_back(BoolePolynomial(*ring));
        poly_valid.push_back(0);
        factors.push_back(f);
        eq_len.push_back(product_size(f));
        return true;
    }

    // a plain polynomial: balanced summation of the terms
    vector<BoolePolynomial> level;
    level.reserve(terms.size());
    for (const VarVec& t : terms) {
        BooleMonomial m(*ring);
        for (const uint32_t v : t) m *= ring->variable(v);
        level.push_back(BoolePolynomial(m));
    }
    while (level.size() > 1) {
        vector<BoolePolynomial> next;
        next.reserve(level.size() / 2 + 1);
        for (size_t k = 0; k + 1 < level.size(); k += 2) {
            next.push_back(level[k] + level[k + 1]);
        }
        if (level.size() % 2) next.push_back(level.back());
        level.swap(next);
    }
    BoolePolynomial poly(*ring);
    if (!level.empty()) poly = level.front();
    return addBoolePolynomial(poly);
}

bool ANF::addProduct(const vector<Lineral>& f_in)
{
    vector<Lineral> f(f_in);
    if (!normalize_product(f)) return false; // identically 0: nothing to add
    if (f.size() < 2) {
        return addBoolePolynomial(expand_linerals(*ring, f));
    }
    const VarVec key = product_key(f);
    if (!prod_keys.insert(key).second) return false;
    BooleMonomial used(*ring);
    for (const Lineral& l : f) {
        for (const uint32_t v : l.vars) used *= ring->variable(v);
    }
    addPolyToOccur(used, eqs.size());
    eqs.push_back(BoolePolynomial(*ring));
    poly_valid.push_back(0);
    factors.push_back(f);
    eq_len.push_back(product_size(f));
    return true;
}

BooleMonomial ANF::varsOf(size_t idx) const
{
    if (poly_valid[idx]) return eqs[idx].usedVariables();
    BooleMonomial used(*ring);
    for (const Lineral& l : factors[idx]) {
        for (const uint32_t v : l.vars) used *= ring->variable(v);
    }
    return used;
}

VarVec ANF::varsVecOf(size_t idx) const
{
    VarVec out;
    if (poly_valid[idx]) {
        for (const uint32_t v : eqs[idx].usedVariables()) out.push_back(v);
        return out; // already sorted
    }
    for (const Lineral& l : factors[idx]) out.insert(out.end(), l.vars.begin(), l.vars.end());
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

size_t ANF::nVarsOf(size_t idx) const
{
    if (poly_valid[idx]) return eqs[idx].nUsedVariables();
    size_t n = 0;
    for (const Lineral& l : factors[idx]) n += l.vars.size();
    return n;
}

bool ANF::eraseKey(size_t idx)
{
    if (!factors[idx].empty()) return prod_keys.erase(product_key(factors[idx])) == 1;
    return eqs_hash.erase(eqs[idx].hash()) == 1;
}

bool ANF::insertKey(size_t idx)
{
    if (!factors[idx].empty()) return prod_keys.insert(product_key(factors[idx])).second;
    return eqs_hash.insert(eqs[idx].hash()).second;
}

bool ANF::addLearntBoolePolynomial(const BoolePolynomial& poly)
{
    // Contextualize it to existing knowledge
    BoolePolynomial contextualized_poly = replacer->update(poly);
    bool added = addBoolePolynomial(contextualized_poly);
    if (added && config.verbosity >= 6) {
        cout << "c Adding: " << poly << endl
             << "c as    : " << contextualized_poly << endl;
    }

    return added;
}

// Slow. O(n^2) because cannot use set<> for BoolePolynomial; KMACHAI: don't understand this comment.....
void ANF::contextualize(vector<BoolePolynomial>& learnt) const
{
    for (size_t i = 0; i < learnt.size(); ++i)
        learnt[i] = replacer->update(learnt[i]);
}

void ANF::addPolyToOccur(const BooleMonomial& mono, const size_t eq_idx)
{
    for (const uint32_t var_idx : mono) {
        occur[var_idx].push_back(eq_idx);
    }
}

void ANF::removePolyFromOccur(const BooleMonomial& mono, size_t eq_idx)
{
    //Remove from occur
    for (const uint32_t var_idx : mono) {
        vector<size_t>::iterator findIt =
            std::find(occur[var_idx].begin(), occur[var_idx].end(), eq_idx);
        assert(findIt != occur[var_idx].end());

        // use swap to erase, because order doesn't matter
        *findIt = occur[var_idx].back();
        occur[var_idx].pop_back();
    }
}

inline void ANF::addPolyToOccur(const BoolePolynomial& poly,
                                const size_t eq_idx)
{
    addPolyToOccur(poly.usedVariables(), eq_idx);
}

inline void ANF::removePolyFromOccur(const BoolePolynomial& poly, size_t eq_idx)
{
    removePolyFromOccur(poly.usedVariables(), eq_idx);
}

ANF::SubstResult ANF::substituted_factors(size_t idx, vector<Lineral>& out)
{
    out.clear();
    vector<Lineral>& f = factors[idx];
    if (f.empty()) {
        // a polynomial that still is a clean product? factor it now,
        // before it is changed
        if (!factor_into_linerals(eqs[idx], f) || f.size() < 2) {
            f.clear();
            return subst_unknown;
        }
    }
    out = f;
    // apply what the replacer knows to every factor
    BooleMonomial used(*ring);
    for (const Lineral& l : out) {
        for (const uint32_t v : l.vars) used *= ring->variable(v);
    }
    for (const uint32_t v : used) {
        const lbool val = replacer->getValue(v);
        if (val != l_Undef) {
            if (!subst_lineral_const(out, v, val == l_True)) return subst_zero;
            continue;
        }
        const Lit lit = replacer->getReplaced(v);
        if (lit.var() != v) {
            if (!subst_lineral_var(out, v, lit.var(), lit.sign())) return subst_zero;
        }
    }
    if (out.empty()) return subst_unsat;   // every factor became 1: 1 = 0
    if (out.size() == 1) return subst_linear;
    return subst_product;
}

bool ANF::updateEquations(size_t eq_idx, const BoolePolynomial newpoly,
                          vector<size_t>& empty_equations,
                          const vector<Lineral>* newfactors)
{
    const VarVec prev_used = varsVecOf(eq_idx);
    const bool erased = eraseKey(eq_idx);
    assert(erased);
    (void)erased;

    bool removed = false;
    vector<Lineral> nf;
    if (newfactors != nullptr) {
        nf = *newfactors;
        if (!normalize_product(nf)) nf.clear(); // identically 0: the equation is trivially true
    }
    if (newfactors != nullptr && nf.empty() && !newfactors->empty()) {
        // a product that became identically 0
        factors[eq_idx].clear();
        eqs[eq_idx] = BoolePolynomial(*ring);
        poly_valid[eq_idx] = 1;
        eq_len[eq_idx] = 0;
        removed = true;
    } else if (newfactors != nullptr && nf.size() == 1) {
        // collapsed to a single factor: a linear polynomial
        factors[eq_idx].clear();
        eqs[eq_idx] = expand_linerals(*ring, nf);
        poly_valid[eq_idx] = 1;
        eq_len[eq_idx] = eqs[eq_idx].length();
        if (!insertKey(eq_idx)) removed = true;
    } else if (newfactors != nullptr && nf.size() >= 2) {
        // stays a product: no polynomial is built
        factors[eq_idx] = nf;
        eqs[eq_idx] = BoolePolynomial(*ring);
        poly_valid[eq_idx] = 0;
        eq_len[eq_idx] = product_size(nf);
        if (!insertKey(eq_idx)) removed = true; // duplicate product
    } else {
        factors[eq_idx].clear();
        eqs[eq_idx] = newpoly;
        poly_valid[eq_idx] = 1;
        eq_len[eq_idx] = newpoly.length();
        if (newpoly.isConstant()) {
            if (newpoly.isOne()) {
                replacer->setNOTOK();
                cout << "Replacer NOT OK" << endl;
                return false;
            }
            removed = true;
        } else if (!insertKey(eq_idx)) {
            removed = true; // duplicate polynomial
        }
    }
    if (removed) {
        factors[eq_idx].clear();
        eqs[eq_idx] = BoolePolynomial(*ring);
        poly_valid[eq_idx] = 1;
        eq_len[eq_idx] = 0;
        empty_equations.push_back(eq_idx);
        if (config.verbosity >= 4) {
            cout << "c    update remove equation " << eq_idx << endl;
        }
    }

    // occurrence lists: drop the variables that went away, add the new ones
    const VarVec curr_used = varsVecOf(eq_idx);
    VarVec gone, came;
    std::set_difference(prev_used.begin(), prev_used.end(), curr_used.begin(), curr_used.end(), std::back_inserter(gone));
    std::set_difference(curr_used.begin(), curr_used.end(), prev_used.begin(), prev_used.end(), std::back_inserter(came));
    for (const uint32_t v : gone) {
        vector<size_t>& occ = occur[v];
        auto it = std::find(occ.begin(), occ.end(), eq_idx);
        assert(it != occ.end());
        *it = occ.back();
        occ.pop_back();
    }
    for (const uint32_t v : came) occur[v].push_back(eq_idx);
    return true;
}

bool ANF::propagate()
{
    SimpStatsScope scope(*this, "anf-prop");
    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [ANF prop] Running ANF propagation..." << endl;
    }

    //When a polynomial updates some var's definition, this set is updated. Used during simplify & addBoolePolynomial
    unordered_set<uint32_t> updatedVars;
    size_t updates = 0;

    // Always run through the new equations
    for (size_t eq_idx = new_equations_begin; eq_idx < eqs.size(); ++eq_idx) {
        // changes: replacer (products never match the patterns)
        if (poly_valid[eq_idx]) updates += check_if_need_update(eqs[eq_idx], updatedVars);
    }

    if (config.verbosity >= 3) {
        cout << "c  "
             << "number of variables to update: " << updatedVars.size()
             << " (caused by " << updates << '/'
             << (eqs.size() - new_equations_begin) << " equations)" << endl;
    }

    std::vector<size_t> empty_equations;
    const bool ret = propagate_iteratively(updatedVars, empty_equations);

    if (config.verbosity) {
        cout << "c [ANF prop] Left eqs: " << eqs.size() << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return ret;
}

bool ANF::propagate_iteratively(unordered_set<uint32_t>& updatedVars,
                                std::vector<size_t>& empty_equations)
{
    //Recursively update polynomials, while there is something to update
    bool timeout = (cpuTime() > config.maxTime);
    while (!updatedVars.empty() && !timeout) {
        if (config.verbosity >= 4) {
            cout << "c  "
                 << "number of variables to update: " << updatedVars.size()
                 << endl;
        }
        // Make a copy of what variables to iterate through in this cycle
        unordered_set<uint32_t> updatedVars_snapshot;
        updatedVars.swap(updatedVars_snapshot);

        for (unordered_set<uint32_t>::const_iterator pvar_idx =
                 updatedVars_snapshot.begin();
             pvar_idx != updatedVars_snapshot.end() && !timeout; ++pvar_idx) {
            const uint32_t& var_idx = *pvar_idx;
            assert(occur.size() > var_idx);
            // We will remove and add stuff to occur, so iterate over a snapshot
            const vector<size_t> occur_snapshot = occur[var_idx];
            if (config.verbosity >= 5) {
                cout << "c Updating variable " << var_idx << ' '
                     << occur_snapshot.size() << endl;
            }
            for (const size_t& eq_idx : occur_snapshot) {
                assert(eqs.size() > eq_idx);
                if (config.verbosity >= 5) {
                    cout << "c equation stats: " << eq_len[eq_idx] << ' '
                         << eq_idx << '/' << occur_snapshot.size() << ' '
                         << var_idx << '/' << updatedVars_snapshot.size()
                         << ' ' << cpuTime() << endl;
                }

                // does the replacer know anything about this equation's variables?
                {
                    bool touched = false;
                    forEachVar(eq_idx, [&](uint32_t v) {
                        if (replacer->getValue(v) != l_Undef ||
                            replacer->getReplaced(v) != Lit(v, false)) {
                            touched = true;
                        }
                    });
                    if (!touched) continue;
                }

                // An equation with a known factorisation is updated on its
                // factors: no ZDD work at all while it stays a product.
                vector<Lineral> newfactors;
                const SubstResult sr = substituted_factors(eq_idx, newfactors);
                BoolePolynomial newpoly(*ring);
                const vector<Lineral>* nf = nullptr;
                switch (sr) {
                    case subst_product:
                        nf = &newfactors;
                        break;
                    case subst_zero:
                        break; // a factor became 0: the equation is 0 = 0
                    case subst_unsat:
                        replacer->setNOTOK();
                        cout << "Replacer NOT OK: product became 1" << endl;
                        return false;
                    case subst_linear:
                        newpoly = expand_linerals(*ring, newfactors);
                        break;
                    case subst_unknown:
                        newpoly = replacer->update(eqs[eq_idx]);
                        break;
                }
                if (!updateEquations(eq_idx, newpoly, empty_equations, nf)) {
                    return false;
                }

                if (poly_valid[eq_idx] && !eqs[eq_idx].isConstant()) {
                    check_if_need_update(eqs[eq_idx], // changes: replacer
                                         updatedVars); // Add back to occur
                }
            } // for eq_idx
            timeout = (cpuTime() > config.maxTime);
        } //for var
        if (config.verbosity >= 4) {
            cout << "c  ..."
                 << "equations removed: " << empty_equations.size()
                 << std::endl;
        }
        timeout = (cpuTime() > config.maxTime);
    } // while

    // now remove the empty
    removeEquations(empty_equations);

    if (!timeout)
        checkSimplifiedPolysContainNoSetVars(); // May not fully propagate due to timeout

    return true;
}

void ANF::checkSimplifiedPolysContainNoSetVars() const
{
    for (size_t i = 0; i < eqs.size(); i++) {
        forEachVar(i, [&](uint32_t var_idx) {
            if (value(var_idx) != l_Undef) {
                cout << "ERROR: Variable " << var_idx << " is inside equation "
                     << eq(i) << " even though its value is " << value(var_idx)
                     << " !!\n";
                exit(-1);
            }
        });
    }
}

void ANF::removeEquations(std::vector<size_t>& eq2r)
{
    vector<std::pair<size_t, size_t> > remap(eqs.size());
    for (size_t i = 0; i < remap.size(); ++i)
        remap[i] = std::make_pair(i, i);

    for (const size_t i : eq2r) {
        const size_t ii = remap[i].second;
        const BoolePolynomial& eq = eqs[ii];
        assert(eq.isConstant() && eq.isZero());
        if (ii == eqs.size() - 1) {
            eqs.pop_back();
            factors.pop_back();
            eq_len.pop_back();
            poly_valid.pop_back();
        } else {
            eqs[ii] = eqs.back();
            eqs.pop_back();
            factors[ii].swap(factors.back());
            factors.pop_back();
            eq_len[ii] = eq_len.back();
            eq_len.pop_back();
            poly_valid[ii] = poly_valid.back();
            poly_valid.pop_back();
            size_t f = remap[eqs.size()].first;
            remap[f].second = ii;
            remap[ii].first = f;
        }
    }

    //Go through each variable occurance
    for (vector<size_t>& var_occur : occur) {
        //The indexes of the equations have changed. Update them.
        for (size_t& eq_idx : var_occur) {
            eq_idx = remap[eq_idx].second;
            assert(eq_idx < eqs.size());
        }
    }

    // bookkeeping and verbosity
    if (config.verbosity >= 3) {
        cout << "c  removed " << eq2r.size() << " eqs." << endl;
    }

    eq2r.clear();
    new_equations_begin = eqs.size();
}

bool ANF::evaluate(const vector<lbool>& vals) const
{
    bool ret = true;
    for (size_t i = 0; i < eqs.size(); i++) {
        const BoolePolynomial& poly = eq(i);
        lbool lret = evaluatePoly(poly, vals);
        assert(lret != l_Undef);

        //OOps, grave bug in implmenetation
        if (lret != l_True) {
            cout << "Internal ERROR! Solution doesn't satisfy eq '" << poly
                 << "' hash=" << poly.stableHash() << endl;
            exit(-1);
        }

        ret &= (lret == l_True);
    }

    if (replacer != nullptr) {
        bool toadd = replacer->evaluate(vals);
        if (!toadd) {
            cout << "Replacer not satisfied" << endl;
            exit(-1);
        }
        ret &= toadd;
    }
    return ret;
}

void ANF::checkOccur() const
{
    for (const vector<size_t>& var_occur : occur) {
        for (const size_t eq_idx : var_occur) {
            assert(eq_idx < eqs.size());
        }
    }
    if (config.verbosity >= 3) {
        cout << "Sanity check passed" << endl;
    }
}

set<size_t> ANF::get_proj_set() const {
    return replacer->get_proj_map(proj_set);
}

void ANF::get_solution_map(map<uint32_t, VarMap>& ret) const
{
    replacer->get_solution_map(ret);
}

void ANF::print_solution_map(std::ofstream* ofs)
{
    replacer->print_solution_map(ofs);
}
