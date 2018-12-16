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

#include "anf.h"
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <string>
#include "replacer.h"
#include "time_mem.h"

using boost::lexical_cast;
using std::cout;
using std::endl;

ANF::ANF(const polybori::BoolePolyRing* _ring, ConfigData& _config)
    : ring(_ring),
      config(_config),
      replacer(new Replacer),
      new_equations_begin(0) {
    //ensure that the variables are not new
    for (size_t i = 0; i < ring->nVariables(); i++) {
        replacer->newVar(i);
    }

    assert(occur.empty());
    occur.resize(ring->nVariables());
}

ANF::~ANF() {
    if (replacer != nullptr)
        delete replacer;
}

// Copy and cleaned from ANF::readFile
size_t ANF::readFileForMaxVar(const std::string& filename) {
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
            continue;
        }
        bool startOfVar = false;
        bool readInVar = false;
        size_t var = 0;
        for (uint32_t i = 0; i < temp.length(); i++) {
            //Handle description separator ','
            if (temp[i] == ',') {
                startOfVar = false;
                readInVar = false;
                var = 0;
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
                readInVar = false;
                continue;
            }

            //Handle constant '0'
            if (temp[i] == '0' && !startOfVar) {
                readInVar = false;
                continue;
            }

            if (temp[i] == '+') {
                startOfVar = false;
                readInVar = false;
                var = 0;
                continue;
            }

            if (temp[i] == '*') {
                if (!readInVar) {
                    cout << "No variable before \"*\" in equation: \"" << temp
                         << "\"" << endl;
                    exit(-1);
                }

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

        //Set state to starting position
        startOfVar = false;
        readInVar = false;
        var = 0;
    }

    ifs.close();
    return maxVar;
}

size_t ANF::readFile(const std::string& filename) {
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
                               unordered_set<uint32_t>& updatedVars) {
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

bool ANF::addBoolePolynomial(const BoolePolynomial& poly) {
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

bool ANF::addLearntBoolePolynomial(const BoolePolynomial& poly) {
    // Contextualize it to existing knowledge
    BoolePolynomial contextualized_poly = replacer->update(poly);
    bool added = addBoolePolynomial(contextualized_poly);
    if (added && config.verbosity >= 6) {
        cout << "c Adding: " << poly << endl
             << "c as    : " << contextualized_poly << endl;
    }

    return added;
}

void ANF::learnSolution(const vector<lbool>& solution) {
    for (size_t i = 0; i < ring->nVariables(); ++i)
        if (solution[i] != l_Undef) {
            BoolePolynomial poly(solution[i] == l_True, *ring);
            poly += BooleVariable(i, *ring);
            addLearntBoolePolynomial(poly);
        }
}

// Slow. O(n^2) because cannot use set<> for BoolePolynomial; KMACHAI: don't understand this comment.....
void ANF::contextualize(vector<BoolePolynomial>& learnt) const {
    for (size_t i = 0; i < learnt.size(); ++i)
        learnt[i] = replacer->update(learnt[i]);
}

void ANF::addPolyToOccur(const BooleMonomial& mono, const size_t eq_idx) {
    for (const uint32_t var_idx : mono) {
        occur[var_idx].push_back(eq_idx);
    }
}

void ANF::removePolyFromOccur(const BooleMonomial& mono, size_t eq_idx) {
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
                                const size_t eq_idx) {
    addPolyToOccur(poly.usedVariables(), eq_idx);
}

inline void ANF::removePolyFromOccur(const BoolePolynomial& poly,
                                     size_t eq_idx) {
    removePolyFromOccur(poly.usedVariables(), eq_idx);
}

bool ANF::propagate() {
    double startTime = cpuTime();
    unordered_set<uint32_t>
        updatedVars; //When a polynomial updates some var's definition, this set is updated. Used during simplify & addBoolePolynomial

    // Always run through the new equations
    for (size_t eq_idx = new_equations_begin; eq_idx < eqs.size(); ++eq_idx)
        check_if_need_update(eqs[eq_idx], updatedVars); // changes: replacer

    //Recursively update polynomials, while there is something to update
    bool timeout = (cpuTime() > config.maxTime);
    std::vector<size_t> empty_equations;
    while (!updatedVars.empty() && !timeout) {
        if (config.verbosity >= 4) {
            cout << "c ANF loop " << cpuTime() << ' ' << updatedVars.size()
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
            cout << "c  " << empty_equations.size() << std::endl;
        }
        timeout = (cpuTime() > config.maxTime);
    } // while

    // now remove the empty
    removeEquations(empty_equations);
    new_equations_begin = eqs.size();

    if (!timeout && !config.notparanoid)
        checkSimplifiedPolysContainNoSetVars(); // May not fully propagate due to timeout

    if (config.verbosity >= 2) {
        cout << "c [ANF propagation] removed " << empty_equations.size()
             << " equations in " << (cpuTime() - startTime) << " seconds. "
             << eqs.size() << " equations left" << endl;
    }
    return true;
}

void ANF::checkSimplifiedPolysContainNoSetVars() const {
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

void ANF::removeEquations(std::vector<size_t>& eq2r) {
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

bool ANF::evaluate(const vector<lbool>& vals) const {
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

void ANF::checkOccur() const {
    for (const vector<size_t>& var_occur : occur) {
        for (const size_t eq_idx : var_occur) {
            assert(eq_idx < eqs.size());
        }
    }
    if (config.verbosity >= 3) {
        cout << "Sanity check passed" << endl;
    }
}

double ANF::sample_and_clone(vector<BoolePolynomial>& equations,
                             double log2size) const {
    const size_t log2fullsz =
        log2(eqs.size()) +
        2 * log2(getRing().nVariables()); // assume quadratic equations only

    if (config.verbosity > 2) {
        cout << "eqs.size(): " << eqs.size() << endl;
        cout << "log2fullsz:" << log2fullsz << endl;
        cout << "log2size: " << log2size << endl;
    }

    if (log2fullsz < log2size) {
        // Small system, so clone the entire system
        equations = eqs;
        return log2fullsz;
    } else {
        // randomly select equations until a limit
        unordered_set<BooleMonomial::hash_type> unique;
        double log2uniquesz = 0;
        size_t sampled = 1, reject = 0;
        double rej_rate = 0;
        do {
            rej_rate = static_cast<double>(reject) / sampled;
            size_t sel =
                std::floor(static_cast<double>(rand()) / RAND_MAX * eqs.size());
            ++sampled;
            if (!unique.empty() && rej_rate < 0.8) {
                // accept with probability of not increasing then number of monomials
                size_t out = 0;
                for (const BooleMonomial& mono : eqs[sel])
                    if (unique.find(mono.hash()) == unique.end())
                        ++out;
                if (static_cast<double>(rand()) / RAND_MAX <
                    static_cast<double>(out) / eqs[sel].length()) {
                    ++reject;
                    continue; // reject and continue with do-while loop
                }
            }
            equations.push_back(eqs[sel]);
            for (const BooleMonomial& mono : equations.back())
                unique.insert(mono.hash());
            log2uniquesz = log2(unique.size());
        } while (log2(equations.size()) + log2uniquesz < log2size);
        if (config.verbosity >= 3)
            cout << "c  Selected " << equations.size() << '[' << unique.size()
                 << "] equations with rejection rate " << rej_rate << endl;

        return log2uniquesz;
    }
}

int ANF::extendedLinearization(vector<BoolePolynomial>& loop_learnt) {
    if (eqs.size() == 0) {
        if (config.verbosity >= 3) {
            cout << "c System is empty. Skip XL\n";
        }
        return 0;
    }
    //else {
    int num_learnt = 0;
    vector<BoolePolynomial> equations;
    map<uint32_t, vector<BoolePolynomial> > deg_buckets;
    unordered_set<uint32_t> unique_poly_degrees;
    vector<uint32_t> sorted_poly_degrees;

    // Get a copy;
    const size_t XLsample =
        config.XLsample + config.XLsampleX; // amount of expansion allowed
    double numUnique = sample_and_clone(
        equations, config.XLsample); // give some leeway for expansion

    // Put them into degree buckets
    for (const BoolePolynomial& poly : equations) {
        uint32_t poly_deg = poly.deg();
        unique_poly_degrees.insert(poly_deg);
        deg_buckets[poly_deg].push_back(poly);
    }
    sorted_poly_degrees.assign(unique_poly_degrees.begin(),
                               unique_poly_degrees.end());
    sort(sorted_poly_degrees.begin(), sorted_poly_degrees.end());

    // Expansion step
    bool done_expansion = false;
    unsigned long nVars = ring->nVariables();
    for (uint32_t deg = 1; deg <= config.xlDeg && !done_expansion; deg++) {
        for (uint32_t poly_deg : sorted_poly_degrees) {
            const vector<BoolePolynomial>& to_expand = deg_buckets[poly_deg];
            if (config.verbosity >= 3) {
                cout << "c  There are " << to_expand.size()
                     << " polynomials of degree " << poly_deg << endl;
            }
            for (const BoolePolynomial& poly : to_expand) {
                const double log2eqsz = log2(equations.size());
                if (log2eqsz + numUnique > XLsample) {
                    done_expansion = true;
                    break;
                } else {
                    // Ugly hack
                    // To do: Efficient implementation of "nVars choose xlDeg"
                    //        e.g. http://howardhinnant.github.io/combinations.html?
                    if (deg >= 1) {
                        const size_t num_variables_allowed = std::min(
                            nVars,
                            static_cast<size_t>(ceil(exp(
                                log(2) * (XLsample - log2eqsz - numUnique)))));
                        if (config.verbosity >= 3 &&
                            num_variables_allowed < nVars)
                            cout << "c  Allowing " << num_variables_allowed
                                 << " variables in degree one expansion\n";

                        if (num_variables_allowed == nVars) {
                            for (unsigned long i = 0; i < nVars; ++i) {
                                BooleVariable v = ring->variable(i);
                                equations.push_back(BoolePolynomial(v * poly));
                            }
                        } else {
                            unsigned long i = 0;
                            for (size_t n = 0; n < num_variables_allowed; ++n) {
                                double p = static_cast<double>(
                                               num_variables_allowed - n) /
                                           (nVars - i);
                                i += floor(log(static_cast<double>(rand()) /
                                               RAND_MAX) /
                                           log(1 - p));

                                // Need to make this cleaner
                                if (i >= nVars)
                                    i = nVars - 1;

                                if (config.verbosity >= 4)
                                    cout << "c Adding variable " << i
                                         << " in degree one expansion\n";
                                BooleVariable v = ring->variable(i);
                                equations.push_back(BoolePolynomial(v * poly));
                            }
                        }
                        numUnique += log2(num_variables_allowed);
                    }
                    if (deg >= 2) {
                        for (unsigned long i = 0; i < nVars; ++i) {
                            for (unsigned long j = i + 1; j < nVars; ++j) {
                                BooleVariable v1 = ring->variable(i);
                                BooleVariable v2 = ring->variable(j);
                                equations.push_back(
                                    BoolePolynomial(v1 * v2 * poly));
                            }
                        }
                        numUnique += 2 * log2(nVars) - 1;
                    }
                    if (deg >= 3) {
                        for (unsigned long i = 0; i < nVars; ++i) {
                            for (unsigned long j = i + 1; j < nVars; ++j) {
                                for (unsigned long k = j + 1; k < nVars; ++k) {
                                    BooleVariable v1 = ring->variable(i);
                                    BooleVariable v2 = ring->variable(j);
                                    BooleVariable v3 = ring->variable(k);
                                    equations.push_back(
                                        BoolePolynomial(v1 * v2 * v3 * poly));
                                }
                            }
                        }
                        numUnique += 3 * log2(nVars) - 2;
                    }
                }
            } //for poly
            if (done_expansion) {
                break;
            }
        } //for poly_deg
        if (done_expansion) {
            break;
        }
    } // for deg
    // Run GJE after expansion

    GaussJordan gj(equations, *ring, config.verbosity);
    vector<BoolePolynomial> learnt_from_gj;
    const size_t num_linear = gj.run(NULL, &learnt_from_gj);
    if (num_linear == GaussJordan::BAD) {
        replacer->setNOTOK();
        return 0;
    }
    for (BoolePolynomial poly : learnt_from_gj) {
        loop_learnt.push_back(poly);
        num_learnt += addLearntBoolePolynomial(poly);
    }
    return num_learnt;
}

static void subsitute(const BooleMonomial& from_mono,
                      const BoolePolynomial& to_poly, BoolePolynomial& poly) {
    BoolePolynomial quotient = poly / from_mono;

    /*if( quotient.isZero() ) // don't need this check
    return;
  else*/
    if (poly.isSingleton()) // for some reason, we need this special case to prevent segfault in some cases
        poly = quotient * to_poly;
    else if (quotient.isOne()) { // ditto
        poly -= from_mono;
        poly += to_poly;
    } else {
        // construct reminder manually to prevent segfault
        BoolePolynomial reminder(0, poly.ring());
        for (const BooleMonomial& mono : poly) {
            if (!mono.reducibleBy(from_mono)) {
                reminder += mono;
            }
        }
        poly = quotient * to_poly;
        if (!reminder.isZero())
            poly += reminder;
    }
}

// Implementation based on https://infoscience.epfl.ch/record/176270/files/ElimLin_full_version.pdf
int ANF::elimLin(vector<BoolePolynomial>& loop_learnt) {
    if (eqs.empty()) {
        return 0;
    }

    if (config.verbosity) {
        cout << "c Running ElimLin... ring size: " << getRing().nVariables() << endl;
    }

    vector<BoolePolynomial> learnt_equations;
    vector<BoolePolynomial> all_equations;
    int num_learnt = 0;

    // Get a copy;
    sample_and_clone(all_equations, config.ELsample);

    bool timeout = (cpuTime() > config.maxTime);
    bool fixedpoint = false;
    while (!fixedpoint && !timeout) {
        fixedpoint = true;

        // Perform Gauss Jordan
        GaussJordan gj(all_equations, *ring, config.verbosity);
        size_t num_linear = gj.run(&all_equations, NULL);
        if (num_linear == GaussJordan::BAD) {
            replacer->setNOTOK();
            return 0;
        }

        vector<std::pair<size_t, size_t> > linear_idx_nvar;
        for (size_t i = 0; i < all_equations.size(); i++)
            if (all_equations[i].deg() == 1)
                linear_idx_nvar.push_back(
                    std::make_pair(i, all_equations[i].nUsedVariables()));

        assert(num_linear == linear_idx_nvar.size());
        if (num_linear == 0) {
            break;
        }
        if (config.verbosity >= 3) {
            cout << "c  Processing " << num_linear << " linear equations "
                 << "in a system of " << all_equations.size() << " equations"
                 << endl;
        }

        // preprocess the linear equations for efficient back-subsitution from the echleon form
        std::sort(linear_idx_nvar.begin(), linear_idx_nvar.end(),
                  [](const std::pair<size_t, size_t>& x,
                     const std::pair<size_t, size_t>& y) {
                      return x.second < y.second;
                  });

        // Create occurrence list for equations involved
        vector<unordered_set<size_t> > el_occ(ring->nVariables());
        for (size_t idx = 0; idx < all_equations.size(); idx++) {
            const BoolePolynomial& poly = all_equations[idx];
            for (const uint32_t v : poly.usedVariables()) {
                el_occ[v].insert(idx);
            }
        }

        // Iterate through all linear equations
        for (const auto& in : linear_idx_nvar) {
            const size_t linear_idx = in.first;
            const BoolePolynomial& linear_eq = all_equations[linear_idx];
            if (!linear_eq.isConstant()) {
                fixedpoint = false;
                learnt_equations.push_back(linear_eq);

                // Pick variable with best metric to substitute
                BooleMonomial from_mono(*ring);
                size_t best_metric = std::numeric_limits<std::size_t>::max();
                assert(linear_eq.deg() == 1);
                for (const BooleMonomial& mono : linear_eq) {
                    if (mono != BooleConstant(1)) {
                        size_t metric = el_occ[mono.firstIndex()].size();
                        if (metric < best_metric) {
                            best_metric = metric;
                            from_mono = mono;
                        }
                    }
                }
                assert(from_mono.deg() == 1);

                BoolePolynomial to_poly(*ring);
                to_poly = linear_eq - from_mono;

                uint32_t var_to_replace = from_mono.firstIndex();
                if (config.verbosity >= 5) {
                    cout << "c Replacing "
                         << linear_eq.firstTerm().firstVariable() << " with "
                         << to_poly << endl;
                }

                // Eliminate variable from these polynomials
                for (size_t idx : el_occ[var_to_replace]) {
                    BoolePolynomial& poly = all_equations[idx];
                    BooleMonomial prev_used = poly.usedVariables();

                    // Eliminate variable
                    if (linear_idx == idx)
                        poly = 0; // replacing itself
                    else {
                        subsitute(from_mono, to_poly, poly);
                        BooleMonomial curr_used(poly.usedVariables());
                        BooleMonomial gcd = prev_used.GCD(curr_used);
                        prev_used /= gcd; // update remove list
                        curr_used /= gcd; // update insert list
                        // Add back to el_occ
                        for (const uint32_t v : curr_used) {
                            auto ins = el_occ[v].insert(idx);
                            assert(ins.second);
                        }
                    } // if

                    for (const uint32_t v : prev_used) {
                        if (v != var_to_replace) {
                            size_t e = el_occ[v].erase(idx);
                            assert(e == 1);
                        }
                    }
                } // for idx
            }
        }
        timeout = (cpuTime() > config.maxTime);
    } // while(fixedpint && timeout)

    // Contextualize final system
    contextualize(all_equations);

    // Add possible useful knowledge back to actual ANF system
    for (BoolePolynomial& poly : all_equations) {
        // 1) Linear equations (includes assignments and anti/equivalences)
        // 2) abc...z + 1 = 0
        // 3) mono1 + mono2 = 0/1 [ Not done ]
        if (poly.deg() == 1 || (poly.isPair() && poly.hasConstantPart())) {
            learnt_equations.push_back(poly);
        }
    }

    // Add learnt_equations
    int linear_count = 0;
    for (const BoolePolynomial& poly : learnt_equations) {
        loop_learnt.push_back(poly);
        bool added = addLearntBoolePolynomial(poly);
        if (added) {
            num_learnt++;
            if (poly.deg() == 1) {
                linear_count++;
            }
        }
    }
    return num_learnt;
}
