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
#include <cstdio>
#include <fstream>
#include <string>
#include <boost/lexical_cast.hpp>
#include <iomanip>

#include "replacer.hpp"
#include "time_mem.h"
#include <map>

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

const std::string var_letters = "abdefghijklmnoqrstuvwxyz"; // no 'c' or 'p'

bool isCharInStr(const char c, const std::string& s) {
    for (uint32_t i = 0; i < s.length(); i++) {
        if (s[i] == c) return true;
    }
    return false;
}

size_t ANF::readFileForNumVars(const std::string& filename)
{
    // Read in the file line by line
    size_t maxes[26]  = {0};
    char varLetter;
    size_t var = 0;
    bool start_bracket = false;

    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        cout << "Problem opening file: \"" << filename << "\" for reading\n";
        exit(-1);
    }

    std::string temp;
    while (std::getline(ifs, temp)) {
        // Empty lines and comments (esp numbers within) are ignored
        if (temp.length() == 0)
            continue;

        char tmp_char =  temp[0];

        if (isCharInStr(tmp_char, "cC"))
            continue;

        // simply search for consecutive numbers
        // however "+1" is a NUMBER but not a MONOMIAL
        // so we have to take that into consideration

        bool isMonomial = false;
        for (uint32_t i = 0; i < temp.length(); ++i) {
            //At this point, only numbers are valid
            if (!std::isdigit(temp[i])) {
                var = 0;

                if (temp[i] == '_')
                    continue;

                if (temp[i] == ')') {
                    if (!start_bracket) {
                        cout << "ERROR: close bracket but no start bracket?" << endl;
                        exit(-1);
                    }
                    start_bracket = false;
                    continue;
                }
                if (temp[i] == '(') {
                    if (start_bracket) {
                        cout << "ERROR: start bracket but previous not closed?" << endl;
                        exit(-1);
                    }
                    start_bracket = true;
                    continue;
                }
                tmp_char = tolower(temp[i]);
                if (isCharInStr(tmp_char,var_letters)) {
                    varLetter = tmp_char;
                    isMonomial = true;
                } else {
                    if (!isascii(temp[i])) {
                        cout << "ERROR: Unknown character 0x" << (int)temp[i] << " (" << temp[i] << ")"
                             << " at position " << i
                             << " in equation: \"" << temp << "\"" << endl;
                        exit(-1);
                    }
                    isMonomial = false;
                }
            } else if (isMonomial) {
                var = var * 10 + (temp[i] - '0');
                int index = (varLetter-'a'); // variables are zero based
                //printf("%c %zu ", varLetter, var);
                if (maxes[index] <= var) {
                    maxes[index] = var+1;
                }
            }
        }
    }
    ifs.close();

    size_t maxVar = 0;
    for(size_t i=0;i<26;i++) {
        maxVar += maxes[i];
    }
    cout << "c readFileForMaxVar: Variables used: " << maxVar << endl;
    return maxVar+2;
}

size_t ANF::readANFFromFile(const std::string& filename)
{
    // Read in the file line by line
    vector<std::string> text_file;
    size_t maxes[26]  = {0};

    size_t currentVar = 2;
    bool proj_set_found = false;

    std::ifstream ifs;
    ifs.open(filename.c_str());
    if (!ifs) {
        cout << "Problem opening file: \"" << filename << "\" for reading\n";
        exit(-1);
    }

    std::string resolve_filename;
    resolve_filename = filename + ".resolve";
    std::ofstream rfs(resolve_filename.c_str());
    if (!rfs) {
        cout << "Problem creating file: \"" << resolve_filename << "\" for writing\n";
        ifs.close();
        exit(-1);
    }

    rfs << "0 signifies 0" << endl << "1 signifies 1" << endl;

    std::string temp;

    while (std::getline(ifs, temp)) {
        // Empty lines are ignored
        if (temp.length() == 0) {
            continue;
        }

        // Save comments
        if (temp.length() > 0 && temp[0] == 'c') {
            comments.push_back(temp);
            if (!proj_set.empty()) {
                cout << "ERROR: you have more than one 'c p show' in your ANF file, i.e. more than one projection set. This is not allowed." << endl;
                exit(-1);
            }
            std::istringstream iss(temp);
            std::string txt;
            iss >> txt;
            if (txt != "c") continue;
            iss >> txt;
            if (txt != "p") continue;
            iss >> txt;
            if (txt != "show") continue;
            proj_set_found = true;
            while(true) {
                iss >> txt;
                if (txt == "END") break;
                int i;
                if(sscanf(txt.c_str(), "x%d", &i) != 1) {
                    cout << "ERROR: in projection set, there is a malformed value, it should be xNUM, but it's: '" << txt << "'" << endl;
                    exit(-1);
                }
                if (i < 0) {
                    cout << "ERROR: projection set must ONLY contain positive integers" << endl;
                    exit(-1);
                }
                if (proj_set.find(i) != proj_set.end()) {
                    cout << "ERROR: you are either adding '" << i << "' twice into the projection set, or your forgot to type 'END' at the end of the projection set" << endl;
                    exit(-1);
                }
                proj_set.insert(i);
            }
            continue;
        }

        BoolePolynomial eq(*ring);
        BoolePolynomial eqDesc(*ring);
        bool startOfVar = false;
        char varLetter;
        char temp_char;
        bool readInVar = false;
        bool readInDesc = false;
        bool start_bracket = false;

        size_t var = 0;
        BooleMonomial m(*ring);

        for (uint32_t i = 0; i < temp.length(); i++) {
            //Handle description separator ','
            if (temp[i] == ',') {
                if (readInVar) {
                    if (!varMapping.containsPair(varLetter, var)) {
                        varMapping.insert(varLetter,var,currentVar);
                        rfs << currentVar << " signifies " << varLetter << "_" << var << endl;
                        var = currentVar;
                        currentVar ++;
                    }
                    else
                        var = varMapping.getVarFromPair(varLetter, var);

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

            // Silently ignore brackets and _
            // This makes the 'parser' work for both "x3" and "x(3)"
            if (temp[i] == '_') {
                continue;
            }

            if (temp[i] == ')') {
                if (!start_bracket) {
                    cout << "ERROR: close bracket but no start bracket?" << endl;
                    exit(-1);
                }
                start_bracket = false;
                continue;
            }
            if (temp[i] == '(') {
                if (start_bracket) {
                    cout << "ERROR: start bracket but previous not closed?" << endl;
                    exit(-1);
                }
                start_bracket = true;
                continue;
            }

            // Space means end of variable
            if (temp[i] == ' ') {
                if (startOfVar && !readInVar) {
                    cout << "x is not followed by number at this line : \""
                         << temp << "\"" << endl;
                    exit(-1);
                }
                startOfVar = false;
                continue;
            }

            temp_char = tolower(temp[i]);
            if (isCharInStr(temp_char,var_letters)) {
                startOfVar = true;
                varLetter = temp_char;
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

            if (temp[i] == '+' || temp[i] == '=') {
                if (start_bracket) {
                    cout << "ERROR: You are adding monomials, but haven't closed the previous one! We can only parse ANF, not e.g. ternary factorized systems" << endl;
                    exit(-1);
                }
                if (readInVar) {
                    if (!varMapping.containsPair(varLetter, var)) {
                        varMapping.insert(varLetter,var,currentVar);
                        rfs << currentVar << " signifies " << varLetter << "_" << var << endl;
                        var = currentVar;
                        currentVar ++;
                    }
                    else
                        var = varMapping.getVarFromPair(varLetter, var);

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
                    cout << "ERROR: No variable before \"*\" at position "
                        << i << " in equation: \"" << temp
                         << "\"" << endl;
                    exit(-1);
                }

                //Multiplying current var into monomial
                if (!varMapping.containsPair(varLetter, var)) {
                    varMapping.insert(varLetter,var,currentVar);
                    rfs << currentVar << " signifies " << varLetter << "_" << var << endl;
                    var = currentVar;
                    currentVar ++;
                }
                else
                    var = varMapping.getVarFromPair(varLetter, var);

                m *= BooleVariable(var, *ring);

                startOfVar = false;
                readInVar = false;
                var = 0;
                continue;
            }

            //Deal with carriage return. Thanks Windows!
            if (temp[i] == 13) {
                continue;
            }

            //At this point, only numbers are valid
            if (temp[i] < '0' || temp[i] > '9') {
                cout << "ERROR: Unknown character 0x" << (int)temp[i] << " (" << temp[i] << ")"
                     << " at position " << i
                     << " in equation: \"" << temp << "\"" << endl;
                exit(-1);
            }

            if (!startOfVar) {
                cout << "ERROR: Value of variable is BEFORE \"x\" in the equation: \"" << temp
                     << "\"" << endl;
                exit(-1);
            }
            readInVar = true;
            int vartmp = temp[i] - '0';
            assert(vartmp >= 0 && vartmp <= 9);
            var *= 10;
            var += vartmp;
            int index = varLetter-'a';
            if (maxes[index] <= var) {
                maxes[index] = var+1;
            }
        }

        //If variable was being built up when the line ended, add it
        if (readInVar) {
            if (!varMapping.containsPair(varLetter, var)) {
                varMapping.insert(varLetter,var,currentVar);
                rfs << currentVar << " signifies " << varLetter << "_" << var << endl;
                var = currentVar;
                currentVar ++;
            }
            else
                var = varMapping.getVarFromPair(varLetter, var);

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
        if (start_bracket) {
            cout << "ERROR: end of line but bracket not closed" << endl;
            exit(-1);
        }
    }

    currentVar --;
    for(const auto& v: proj_set) {
        if (v > currentVar) {
            cout << "ERROR: the maximum variable in the ANF was: x" << currentVar << " but your projection set contains var x" << v << " which is higher. This is wrong." << endl;
            exit(-1);
        }
    }

    if (proj_set_found == false) {
        cout << "c setting projection set to ALL variables since we didn't find a 'c p show ... END'" << endl;
        for(uint32_t i = 0; i <= currentVar; i++) {
            proj_set.insert(i);
        }
    }

    rfs.close();
    ifs.close();
    cout << "c readFile: Variables used: " << currentVar << endl;
    return currentVar;
}

void print_solution_map(std::ofstream* ) { }

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

bool ANF::updateEquations(size_t eq_idx, const BoolePolynomial newpoly,
                          vector<size_t>& empty_equations)
{
    BoolePolynomial& poly = eqs[eq_idx];
    BooleMonomial prev_used = poly.usedVariables();

    const size_t check = eqs_hash.erase(poly.hash());
    assert(check == 1);
    poly = newpoly;

    if (poly.isConstant()) {
        //Check UNSAT
        if (poly.isOne()) {
            replacer->setNOTOK();
            cout << "Replacer NOT OK" << endl;
            return false;
        }
        empty_equations.push_back(eq_idx);
        if (config.verbosity >= 4) {
            cout << "c    update remove equation " << eq_idx << endl;
        }
    } else {
        auto ins = eqs_hash.insert(poly.hash());
        if (!ins.second) { // already exist
            poly = 0;      // remove it using empty
            empty_equations.push_back(eq_idx);
            if (config.verbosity >= 4) {
                cout << "c [ANF propagation remove equation] " << eq_idx
                     << endl;
            }
        }
    } // if ... else

    BooleMonomial curr_used(poly.usedVariables());
    BooleMonomial gcd = prev_used.GCD(curr_used);
    prev_used /= gcd; // update remove list
    curr_used /= gcd; // update insert list
    removePolyFromOccur(prev_used, eq_idx);
    addPolyToOccur(curr_used, eq_idx);
    return true;
}

bool ANF::propagate()
{
    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [ANF prop] Running ANF propagation..." << endl;
    }

    //When a polynomial updates some var's definition, this set is updated. Used during simplify & addBoolePolynomial
    unordered_set<uint32_t> updatedVars;
    size_t updates = 0;

    // Always run through the new equations
    for (size_t eq_idx = new_equations_begin; eq_idx < eqs.size(); ++eq_idx) {
        // changes: replacer
        updates += check_if_need_update(eqs[eq_idx], updatedVars);
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

                if (!(replacer->willUpdate(poly))) {
                    continue;
                }

                if (!updateEquations(eq_idx, replacer->update(poly),
                                     empty_equations)) {
                    return false;
                }

                if (!poly.isConstant()) {
                    check_if_need_update(poly,         // changes: replacer
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
