/*****************************************************************************
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

#include <fstream>
#include <iostream>
#include <sstream>

#include "dimacscache.hpp"

std::vector<Clause> DIMACSCache::clauses;
size_t DIMACSCache::maxVar;
const char* DIMACSCache::fname = nullptr;

DIMACSCache::DIMACSCache(const char* _fname)
{
    if (_fname == fname)
        return;
    fname = _fname;

    maxVar = 0;
    clauses.clear();

    std::ifstream ifs;
    std::string temp;
    std::string x;
    ifs.open(fname);
    if (!ifs) {
        std::cout << "Problem opening file: " << fname << " for reading\n";
        exit(-1);
    }

    while (std::getline(ifs, temp)) {
        if (temp.length() == 0 || temp[0] == 'p' || temp[0] == 'c') {
            continue;
        } else {
            std::istringstream iss(temp);
            vector<Lit> lits;
            while (iss.good() && !iss.eof()) {
                iss >> x;
                int v = stoi(x);
                if (v == 0) {
                    clauses.push_back(Clause(lits));
                    break;
                } else {
                    lits.push_back(Lit(abs(v) - 1, v < 0));
                }
                maxVar = std::max(maxVar, (size_t)abs(stoi(x)));
            }
        }
    }
}
