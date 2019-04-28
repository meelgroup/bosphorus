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

#include "karnaugh.hpp"
#include <iostream>
#include "evaluator.hpp"

extern "C" {
extern void minimise_karnaugh(int no_inputs, int no_outputs, int** input,
                              int** output, int* no_lines, bool onlymerge);
}

using std::cout;
using std::endl;
using std::map;

using namespace BosphLib;

Karnaugh::Karnaugh(uint32_t maxKarnTableSize)
{
    maxKarnTable = maxKarnTableSize;
    karnSize = (0x1UL) << maxKarnTable;

    input = new int*[2 * karnSize]; // Do bulk allocation for input and output
    input[0] = new int[(3 + maxKarnTable) * karnSize];
    for (uint i = 1; i < karnSize; i++)
        input[i] = input[i - 1] + maxKarnTable;

    output = input + karnSize;
    output[0] = input[karnSize - 1] + maxKarnTable;
    for (uint i = 1; i < karnSize; i++)
        output[i] = output[i - 1] + 3;
}

Karnaugh::~Karnaugh()
{
    delete[] input[0];
    delete[] input;
}

vector<Clause> Karnaugh::getClauses()
{
    vector<Clause> clauses;
    for (int i = 0; i < no_lines[1]; i++) {
        assert(output[i][0] == 1);
        vector<Lit> lits;
        for (size_t i2 = 0; i2 < tableVars.size(); i2++) {
            if (input[i][i2] != 2)
                lits.push_back(Lit(tableVars[i2], input[i][i2]));
        }
        clauses.push_back(Clause(lits));
    }
    return clauses;
}

void Karnaugh::print() const
{
    for (int i = 0; i < no_lines[0] + no_lines[1]; i++) {
        for (size_t i2 = 0; i2 < tableVars.size(); i2++) {
            cout << input[i][i2] << " ";
        }
        cout << "-- " << output[i][0] << endl;
    }
    cout << "--------------" << endl;
}

void Karnaugh::evaluateIntoKarn(const BoolePolynomial& eq)
{
    // Move vars to tablevars
    tableVars.clear();
    for (const uint32_t v : eq.usedVariables()) {
        tableVars.push_back(v);
    }

    // Check if we can at all represent it
    if (maxKarnTable < tableVars.size()) {
        cout << "equation: " << eq << " (vars in poly:";
        std::copy(tableVars.begin(), tableVars.end(),
                  std::ostream_iterator<uint32_t>(cout, ","));
        cout << ")" << endl;
        cout << "max_var in equationstosat.cpp is too small!" << endl;
        exit(-1);
    }

    map<uint32_t, uint32_t> outerToInter;
    uint32_t i = 0;
    for (const uint32_t v : tableVars) {
        outerToInter[v] = i;
        i++;
    }

    //Put in the whole truth table
    no_lines[0] = 0;
    no_lines[1] = 0;
    no_lines[2] = 0;
    for (uint32_t setting = 0; setting < ((uint32_t)0x1 << tableVars.size());
         setting++) {
        // Variable setting
        for (size_t i = 0; i < tableVars.size(); i++) {
            uint val = (setting >> i) & 0x1;
            input[no_lines[1]][i] = val;
        }

        // Evaluate now
        lbool lout = evaluatePoly(eq, setting, outerToInter);
        assert(lout != l_Undef);
        bool out = (lout == l_True);
        if (out) {
            output[no_lines[1]][0] = 1;
            no_lines[1]++;
        }
    }
}

vector<Clause> Karnaugh::convert(const BoolePolynomial& eq)
{
    evaluateIntoKarn(eq);
    minimise_karnaugh(tableVars.size(), 1, input, output, no_lines, false);
    return getClauses();
}
