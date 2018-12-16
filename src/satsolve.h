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
******************************************************************************/

#ifndef SATSOLVE__H
#define SATSOLVE__H

#include <stdio.h>
#include <boost/lexical_cast.hpp>
#include <string>

#include "anf.h"
#include "cnf.h"

using std::cout;
using std::endl;
using std::string;

inline bool testSolution(const ANF& anf, const vector<lbool>& solution) {
    bool goodSol = anf.evaluate(solution);
    if (!goodSol) {
        cout << "ERROR! Solution found is incorrect!" << endl;
        exit(-1);
    }
    return goodSol;
}

inline void printSolution(const vector<lbool>& solution) {
    size_t num = 0;
    std::stringstream toWrite;
    toWrite << "v ";
    for (const lbool lit : solution) {
        if (lit != l_Undef) {
            toWrite << ((lit == l_True) ? "" : "-") << num << " ";
        }
        num++;
    }
    cout << toWrite.str() << endl;
}

class SATSolve {
   public:
    SATSolve(const int verbosity, const bool testsolution,
             string solverExecutable);

    vector<lbool> solveCNF(const ANF* orig_anf, const ANF& anf, const CNF& cnf);
    const vector<lbool>& getSolution() const {
        return solution;
    }
    const lbool getSatisfiable() const {
        return satisfiable;
    }

   private:
    void createChildProcess();
    string readchild();
    void error(const char* s);

    //Managing processes
    int in[2];
    int out[2];
    int pid; //process ID

    //Solution
    vector<lbool> solution;
    lbool satisfiable;

    //Parameters
    string solverExecutable;
    int verbosity;
    bool testsolution;
};

#endif //SATSOLVE__H
