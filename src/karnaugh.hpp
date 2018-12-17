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

#ifndef _KARNAUGH_H_
#define _KARNAUGH_H_

#include "clauses.hpp"
#include "cryptominisat5/solvertypesmini.h"
#include "polybori.h"
using CMSat::lbool;
using CMSat::Lit;

USING_NAMESPACE_PBORI

extern "C" {
extern void minimise_karnaugh(int no_inputs, int no_outputs, int** input,
                              int** output, int* no_lines, bool onlymerge);
}

class Karnaugh
{
   public:
    Karnaugh(uint32_t maxKarnTableSize)
    {
        maxKarnTable = maxKarnTableSize;
        karnSize = (0x1UL) << maxKarnTable;

        input = new int*[karnSize];
        for (uint i = 0; i < karnSize; i++)
            input[i] = new int[maxKarnTable];

        output = new int*[karnSize];
        for (uint i = 0; i < karnSize; i++)
            output[i] = new int[3];
    }

    ~Karnaugh()
    {
        for (uint i = 0; i < karnSize; i++)
            delete[] input[i];
        delete[] input;

        for (uint i = 0; i < karnSize; i++)
            delete[] output[i];
        delete[] output;
    }

    bool possibleToConv(const BoolePolynomial& eq);
    vector<Clause> convert(const BoolePolynomial& eq);
    void print() const;

   private:
    void evaluateIntoKarn(const BoolePolynomial& eq);
    vector<Clause> getClauses();
    int no_lines[3];
    int** input;  ///<The input
    int** output; ///<The output
    vector<uint32_t>
        tableVars; ///<The variables that correspond to each column in the Karnaugh table
    uint32_t karnSize;
    uint32_t maxKarnTable;
};

#endif //_KARNAUGH_H_
