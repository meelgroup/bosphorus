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

#include "clause.hpp"
#include "solvertypesmini.h"
#include "polybori.h"

USING_NAMESPACE_PBORI

namespace BLib {

class Karnaugh
{
   public:
    Karnaugh(uint32_t maxKarnTableSize);
    ~Karnaugh();

    bool possibleToConv(const BoolePolynomial& eq) const
    {
        return (maxKarnTable >= eq.nUsedVariables());
    }
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

}

#endif //_KARNAUGH_H_
