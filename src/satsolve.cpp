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

#include <cerrno>
#include <sstream>
#include <string>

#include "satsolve.hpp"

using std::cout;
using std::endl;

SATSolve::SATSolve(const int _verbosity, const bool _testsolution,
                   string _solverExecutable)
    : satisfiable(l_Undef),
      solverExecutable(_solverExecutable),
      verbosity(_verbosity),
      testsolution(_testsolution)
{
}

void SATSolve::createChildProcess()
{
    pid = fork();

    if (pid == -1) {
        cout << "Couldn't start child process!" << endl;
        cout << "Error: " << strerror(errno) << endl;
        exit(-1);
    }

    if (pid == 0) {
        // This is the child process

        // Close stdin, stdout, stderr in the child process
        close(0);
        close(1);
        close(2);

        // make our pipes, our new stdin,stdout and stderr
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);

        /* Close the other ends of the pipes that the parent will use, because if
        * we leave these open in the child, the child/parent will not get an EOF
        * when the parent/child closes their end of the pipe.
        */
        close(in[1]);
        close(out[0]);

        // Over-write the child process with the binary
        execl(solverExecutable.c_str(), solverExecutable.c_str(), (char*)NULL);

        //If we couldn't overwrite it, it means there is a failure
        cout << "ERROR! Could not execute '" << solverExecutable << "'" << endl;
        exit(-1);
    }
}

string SATSolve::readchild()
{
    // Read back any output
    string str;
    char buf[255];
    int n;
    while ((n = read(out[0], buf, 250)) > 0) {
        buf[n] = 0;
        str += buf;
    }

    return str;
}

vector<lbool> SATSolve::solveCNF(const ANF* orig_anf, const ANF& anf,
                                 const CNF& cnf)
{
    /* In a pipe, xx[0] is for reading, xx[1] is for writing */
    if (pipe(in) < 0)
        error("pipe in");
    if (pipe(out) < 0)
        error("pipe out");

    createChildProcess();

    if (verbosity >= 4) {
        cout << "c Spawned '" << solverExecutable
             << "' as a child process at pid " << pid << endl;
    }

    /* This is the parent process */
    /* Close the pipe ends that the child uses to read from / write to so
    * the when we close the others, an EOF will be transmitted properly.
    */
    close(in[0]);
    close(out[1]);

    // Write data to the child's input
    //cout << "Writing CNF to SAT solver input: " << cnf << endl;
    std::stringstream ss;
    ss << cnf;
    string ss2 = ss.str();

    //Optionally, dump CNF to a file
    /*FILE* f = fopen("tmp.cnf", "w");
    fwrite(ss2.c_str(), ss2.size(), sizeof(char), f);
    fclose(f);*/

    ssize_t ret = write(in[1], ss2.c_str(), ss2.length());

    //Handle errors
    if (ret == -1) {
        cout << "Could not write to child process (maybe '" << solverExecutable
             << "' does not exist?" << endl;
        exit(-1);
    }

    if (ret < (ssize_t)(ss.str().length())) {
        cout << "Could not write full CNF to child process" << endl;
        cout << "Child said: " << endl << readchild() << endl;
        cout << "Possiby early exit... checking output" << endl;
    }

    // The child may block unless we close its input stream.
    // This sends an EOF to the child on its stdin.
    close(in[1]);

    string str = readchild();
    vector<string> solLines;
    string SAT;
    size_t pos = 0;
    cout << "c solver output: " << str << endl;
    while (pos < str.size()) {
        string tmp;
        while (pos < str.size() && str[pos] == '\n')
            pos++;
        if (pos == str.size())
            break;

        while (pos < str.size() && str[pos] != '\n') {
            tmp += str[pos];
            pos++;
        }
        if (tmp.size() == 0)
            continue;

        if (tmp[0] != 'c') {
            if (tmp[0] == 'v') {
                solLines.push_back(tmp);
            } else if (tmp[0] == 's') {
                SAT = tmp;
            } else {
                cout << "ERROR! Line '" << tmp
                     << "' doesn't contain correct starting character" << endl;

                exit(-1);
            }
        }
    }

    if (SAT == "s UNSATISFIABLE") {
        satisfiable = l_False;
    } else if (SAT == "s SATISFIABLE") {
        satisfiable = l_True;
    } else {
        cout << "ERROR! Solution line '" << SAT << "' not recognised" << endl;
        exit(-1);
    }

    cout << SAT << endl;
    if (satisfiable == l_False)
        return vector<lbool>();

    //Satisfiable, so solution has been printed
    //Extract it and verify it
    //std::map<uint32_t, lbool> solutionFromSolver;
    vector<lbool> solutionFromSolver;

    for (vector<string>::const_iterator it = solLines.begin(),
                                        end = solLines.end();
         it != end; it++) {
        const string& str = *it;
        assert(str[0] == 'v');
        size_t pos = 1;
        bool finishing = false;
        while (pos < it->size()) {
            while (str[pos] == ' ' && pos < it->size())
                pos++;
            if (pos == it->size())
                break;

            string tmp;
            while (str[pos] != ' ' && pos < it->size()) {
                tmp += str[pos];
                pos++;
            }
            int val;
            try {
                val = boost::lexical_cast<int>(tmp);
            } catch (boost::bad_lexical_cast&) {
                cout << "Solution from SAT solver contains part '" << tmp
                     << "', which cannot be converted to int!" << endl;

                exit(-1);
            }
            if (val == 0) {
                if (!finishing)
                    finishing = true;
                else {
                    cout << "Finishing value '0' encountered twice while "
                         << "reading solution from SAT solver!" << endl;

                    exit(-1);
                }
                finishing = true;
            } else {
                size_t v = std::abs(val) - 1;
                if (v >= solutionFromSolver.size())
                    solutionFromSolver.resize(v + 1, l_Undef);
                solutionFromSolver[v] = CMSat::boolToLBool(val > 0);
            }
        }
    }

    //Map solution from SAT solver to ANF values
    vector<lbool> solution2 = cnf.mapSolToOrig(solutionFromSolver);

    //Extend the solution to full ANF
    solution = anf.extendSolution(solution2);

    if (verbosity >= 5) {
        printSolution(solution);
    }

    if (orig_anf != nullptr && testsolution) {
        const bool ok = testSolution(*orig_anf, solution);
        assert(ok);
        if (verbosity) {
            cout << "c Solution found is correct." << endl;
        }
    }

    return solution;
}

void SATSolve::error(const char* s)
{
    perror(s);
    exit(1);
}
