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
#include <boost/lexical_cast.hpp>
#include <cctype>
#include <fstream>
#include <string>
#include "replacer.hpp"
#include "time_mem.h"

using boost::lexical_cast;
using std::cout;
using std::endl;

ANF::ANF(const polybori::BoolePolyRing* _ring, ConfigData& _config)
    : ring(_ring),
      config(_config),
      replacer(new Replacer),
      new_equations_begin(0)
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

size_t ANF::readFileForMaxVar(const std::string& filename)
{
    // Read in the file line by line
    size_t maxVar = 0;

    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        cout << "Problem opening file: \"" << filename << "\" for reading\n";
        exit(-1);
    }

    std::string temp;
    while (std::getline(ifs, temp)) {
        // Empty lines and comments (esp numbers within) are ignored
        if (temp.length() == 0 || temp[0] == 'c')
            continue;

        size_t var = 0;
        // simply search for consecutive numbers
        for (uint32_t i = 0; i < temp.length(); ++i) {
            //At this point, only numbers are valid
            if (!std::isdigit(temp[i]))
                var = 0;
            else {
                var = var * 10 + (temp[i] - '0');
                maxVar = std::max(
                    maxVar,
                    var); //This variable will be used, no matter what, so use it as max
            }
        }
    }

    ifs.close();
    return maxVar;
}

size_t ANF::readFile(const std::string& filename)
{
    // Read in the file line by line
    vector<std::string> text_file;

    size_t maxVar = 0;

    std::ifstream ifs;
    ifs.open(filename.c_str());
    if (!ifs) {
        cout << "Problem opening file: \"" << filename << "\" for reading\n";
        exit(-1);
    }
    std::string temp;

    while (std::getline(ifs, temp)) {
        // Empty lines are ignored
        if (temp.length() == 0) {
            continue;
        }

        // Save comments
        if (temp.length() > 0 && temp[0] == 'c') {
            comments.push_back(temp);
            continue;
        }

        BoolePolynomial eq(*ring);
        BoolePolynomial eqDesc(*ring);
        bool startOfVar = false;
        bool readInVar = false;
        bool readInDesc = false;
        size_t var = 0;
        BooleMonomial m(*ring);
        for (uint32_t i = 0; i < temp.length(); i++) {
            //Handle description separator ','
            if (temp[i] == ',') {
                if (readInVar) {
                    m *= BooleVariable(var, *ring);
                    eq += m;
                }

                startOfVar = false;
                readInVar = false;
                var = 0;
                m = BooleMonomial(*ring);
                readInDesc = true;
                continue;
            }

            //Silently ignore brackets.
            //This makes the 'parser' work for both "x3" and "x(3)"
            if (temp[i] == ')' || temp[i] == '(') {
                continue;
            }

            //Space means end of variable
            if (temp[i] == ' ') {
                if (startOfVar && !readInVar) {
                    cout << "x is not followed by number at this line : \""
                         << temp << "\"" << endl;
                    exit(-1);
                }
                startOfVar = false;
                continue;
            }

            if (temp[i] == 'x' || temp[i] == 'X') {
                startOfVar = true;
                readInVar = false;
                continue;
            }

            //Handle constant '1'
            if (temp[i] == '1' && !startOfVar) {
                if (!readInDesc)
                    eq += BooleConstant(true);
                else
                    eqDesc += BooleConstant(true);
                readInVar = false;
                continue;
            }

            //Handle constant '0'
            if (temp[i] == '0' && !startOfVar) {
                if (!readInDesc)
                    eq += BooleConstant(false);
                else
                    eqDesc += BooleConstant(false);
                readInVar = false;
                continue;
            }

            if (temp[i] == '+') {
                if (readInVar) {
                    m *= BooleVariable(var, *ring);

                    if (!readInDesc)
                        eq += m;
                    else
                        eqDesc += m;
                }

                startOfVar = false;
                readInVar = false;
                var = 0;
                m = BooleMonomial(*ring);
                continue;
            }

            if (temp[i] == '*') {
                if (!readInVar) {
                    cout << "No variable before \"*\" in equation: \"" << temp
                         << "\"" << endl;
                    exit(-1);
                }

                //Multiplying current var into monomial
                m *= BooleVariable(var, *ring);

                startOfVar = false;
                readInVar = false;
                var = 0;
                continue;
            }

            //At this point, only numbers are valid
            if (temp[i] < '0' || temp[i] > '9') {
                cout << "Unknown character \"" << temp[i]
                     << "\" in equation: \"" << temp << "\"" << endl;
                exit(-1);
            }

            if (!startOfVar) {
                cout << "Value of var before \"x\" in equation: \"" << temp
                     << "\"" << endl;
                exit(-1);
            }
            readInVar = true;
            int vartmp = temp[i] - '0';
            assert(vartmp >= 0 && vartmp <= 9);
            var *= 10;
            var += vartmp;

            //This variable will be used, no matter what, so use it as max
            maxVar = std::max(maxVar, var);
        }

        //If variable was being built up when the line ended, add it
        if (readInVar) {
            m *= BooleVariable(var, *ring);

            if (!readInDesc)
                eq += m;
            else
                eqDesc += m;
        }

        //Set state to starting position
        startOfVar = false;
        readInVar = false;
        var = 0;
        m = BooleMonomial(*ring);

        size_t realTermsSize = eqDesc.length() - (int)eqDesc.hasConstantPart();
        if (realTermsSize > 1) {
            cout << "ERROR!" << endl
                 << "After the comma, only a monomial is supported (not an "
                    "equation)"
                 << endl
                 << "But You gave: " << eqDesc << " on line: '" << temp << "'"
                 << endl;
            exit(-1);
        }

        if (realTermsSize == 1 && eqDesc.firstTerm().deg() > 1) {
            cout << "ERROR! " << endl
                 << "After the comma, only a single-var monomial is supported "
                    "(no multi-var monomial)"
                 << endl
                 << "You gave: " << eqDesc << " on line: " << temp << endl;
            exit(-1);
        }

        addBoolePolynomial(eq);
    }

    ifs.close();
    return maxVar;
}

// KMA Chai: Check if this polynomial can cause further ANF propagation
bool ANF::check_if_need_update(const BoolePolynomial& poly,
                               unordered_set<uint32_t>& updatedVars)
{
    //
    // Assign values
    //
    // If polynomial is "x = 0" or "x + 1 = 0", set the value of x
    if (poly.length() - (int)poly.hasConstantPart() == 1 && poly.deg() == 1) {
        // Make the update
        uint32_t v = poly.usedVariables().firstVariable().index();
        vector<uint32_t> ret = replacer->setValue(v, poly.hasConstantPart());

        // Mark updated vars
        for (const uint32_t& updated_var : ret) {
            updatedVars.insert(updated_var);
        }
        return true;
    }

    // If polynomial is "a*b*c*.. + 1 = 0", then all variables must be TRUE
    if (poly.length() == 2 && poly.hasConstantPart()) {
        for (const uint32_t& var_idx : poly.firstTerm()) {
            // Make the update
            vector<uint32_t> ret = replacer->setValue(var_idx, true);

            // Mark updated vars
            for (const uint32_t var_idx2 : ret) {
                updatedVars.insert(var_idx2);
            }
        }
        return true;
    }

    //
    // Assign anti/equivalences
    //
    // If polynomial is "x + y = 0" or "x + y + 1 = 0", set the value of x in terms of y
    if (poly.length() - (int)poly.hasConstantPart() == 2 && poly.deg() == 1) {
        BooleMonomial m1 = poly.terms()[0];
        BooleMonomial m2 = poly.terms()[1];

        assert(m1.deg() == 1);
        assert(m2.deg() == 1);
        uint32_t var1 = m1.firstVariable().index();
        uint32_t var2 = m2.firstVariable().index();

        // Make the update
        vector<uint32_t> ret =
            replacer->setReplace(var1, Lit(var2, poly.hasConstantPart()));
        updatedVars.insert(var1);
        updatedVars.insert(var2);

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

    return true;
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

void ANF::learnSolution(const vector<lbool>& solution)
{
    for (size_t i = 0; i < ring->nVariables(); ++i)
        if (solution[i] != l_Undef) {
            BoolePolynomial poly(solution[i] == l_True, *ring);
            poly += BooleVariable(i, *ring);
            addLearntBoolePolynomial(poly);
        }
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

bool ANF::propagate()
{
    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [ANF prop] Running ANF propagation..." << endl;
    }

    unordered_set<uint32_t>
        updatedVars; //When a polynomial updates some var's definition, this set is updated. Used during simplify & addBoolePolynomial

    // Always run through the new equations
    size_t num_initial_updates = 0;
    for (size_t eq_idx = new_equations_begin; eq_idx < eqs.size(); ++eq_idx)
        num_initial_updates += check_if_need_update(eqs[eq_idx], updatedVars); // changes: replacer
    if (config.verbosity >= 3) {
        cout << "c  " << "number of variables to update: "
             << updatedVars.size() << " (caused "
	     << num_initial_updates << '/'
	     << (eqs.size() - new_equations_begin) << " equations)"
             << endl;
    }
    

    //Recursively update polynomials, while there is something to update
    bool timeout = (cpuTime() > config.maxTime);
    std::vector<size_t> empty_equations;
    while (!updatedVars.empty() && !timeout) {
        if (config.verbosity >= 4) {
            cout << "c  " << "number of variables to update: "
		 << updatedVars.size()
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
                BoolePolynomial& poly = eqs[eq_idx];
                if (config.verbosity >= 5) {
                    cout << "c equation stats: " << poly.length() << ' '
                         << poly.nUsedVariables() << ' ' << eq_idx << '/'
                         << occur_snapshot.size() << ' ' << var_idx << '/'
                         << updatedVars_snapshot.size() << ' ' << cpuTime()
                         << endl;
                }

                if (config.verbosity >= 6) {
                    cout << "c equation: " << poly << endl;
                }

                const bool willUpdate = replacer->willUpdate(poly);
                BooleMonomial prev_used(*ring);
                if (willUpdate) {
                    prev_used = poly.usedVariables();
                    const size_t check = eqs_hash.erase(poly.hash());
                    assert(check == 1);
                    poly = replacer->update(poly);
                }

                if (poly.isConstant()) {
                    //Check UNSAT
                    if (poly.isOne()) {
                        replacer->setNOTOK();
                        cout << "Replacer NOT OK" << endl;
                        return false;
                    }
                    empty_equations.push_back(eq_idx);
                    if (config.verbosity >= 4) {
                        cout << "c [ANF propagation remove equation] " << eq_idx
                             << endl;
                    }
                } else {
                    check_if_need_update(poly,
                                         updatedVars); // changes: replacer
                                                       // Add back to occur
                    if (willUpdate) {
                        auto ins = eqs_hash.insert(poly.hash());
                        if (!ins.second) { // already exist
                            poly = 0;      // remove it using empty
                            empty_equations.push_back(eq_idx);
                            if (config.verbosity >= 4) {
                                cout << "c [ANF propagation remove equation] "
                                     << eq_idx << endl;
                            }
                        }
                    }
                } // if ... else

                if (willUpdate) {
                    BooleMonomial curr_used(poly.usedVariables());
                    BooleMonomial gcd = prev_used.GCD(curr_used);
                    prev_used /= gcd; // update remove list
                    curr_used /= gcd; // update insert list
                    removePolyFromOccur(prev_used, eq_idx);
                    addPolyToOccur(curr_used, eq_idx);
                }
            }
            timeout = (cpuTime() > config.maxTime);
        } //for var
        if (config.verbosity >= 4) {
            cout << "c  ..." << "equations removed: " << empty_equations.size() << std::endl;
        }
        timeout = (cpuTime() > config.maxTime);
    } // while

    // now remove the empty
    removeEquations(empty_equations);
    new_equations_begin = eqs.size();

    if (!timeout && config.paranoid)
        checkSimplifiedPolysContainNoSetVars(); // May not fully propagate due to timeout

    if (config.verbosity) {
        cout << "c [ANF prop] removed " << empty_equations.size()
             << " eqs. Left eqs: " << eqs.size() << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return true;
}

void ANF::checkSimplifiedPolysContainNoSetVars() const
{
    for (const BoolePolynomial& poly : eqs) {
        for (const uint32_t var_idx : poly.usedVariables()) {
            if (value(var_idx) != l_Undef) {
                cout << "ERROR: Variable " << var_idx << " is inside equation "
                     << poly << " even though its value is " << value(var_idx)
                     << " !!\n";
                exit(-1);
            }
        }
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
        if (ii == eqs.size() - 1)
            eqs.pop_back();
        else {
            eqs[ii] = eqs.back();
            eqs.pop_back();
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
}

bool ANF::evaluate(const vector<lbool>& vals) const
{
    bool ret = true;
    for (const BoolePolynomial& poly : eqs) {
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
