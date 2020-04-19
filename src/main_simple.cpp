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

#include <sys/wait.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <iomanip>

#include "bosphorus.hpp"
#include "time_mem.h"
#include "bosphorus/solvertypesmini.hpp"

using namespace Bosph;
using std::string;
using std::cout;
using std::endl;

int main() {
    uint32_t maxiters = 1;

    string cnfInput = "out.cnf";
    Bosphorus mylib;
    DIMACS* dimacs = mylib.parse_cnf(cnfInput.c_str());
    ANF* anf = mylib.chunk_dimacs(dimacs);

    auto orig_anf = Bosphorus::copy_anf_no_replacer(anf);

    //simplify, etc.
    mylib.simplify(anf, NULL, maxiters);
    mylib.add_trivial_learnt_from_anf_to_learnt(anf, orig_anf);
    Bosphorus::delete_anf(orig_anf);
    mylib.deduplicate();

    auto cnf = mylib.cnf_from_anf_and_cnf(NULL, anf);
    vector<Clause> cls = mylib.get_clauses(cnf);
    for(auto x: cls) {
        cout << "cl: " << x << endl;
    }

    return 0;
}
