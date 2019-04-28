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

#ifndef _CLAUSES_H_
#define _CLAUSES_H_

#include <limits>
#include <vector>
#include <assert.h>
#include "solvertypesmini.h"
using std::vector;

namespace BLib {

class Clause
{
   public:
    Clause(const vector<Lit>& _lits) : lits(_lits)
    {
    }

    const vector<Lit>& getLits() const
    {
        return lits;
    }

    size_t size() const
    {
        return lits.size();
    }

    bool empty() const
    {
        return lits.empty();
    }

    vector<Lit> getClause() const
    {
        assert(!lits.empty());
        return lits;
    }

    friend std::ostream& operator<<(std::ostream& os, const Clause& cl);

   private:
    vector<Lit> lits;
};

inline std::ostream& operator<<(std::ostream& os, const Clause& cl)
{
    for (vector<Lit>::const_iterator it = cl.lits.begin(), end = cl.lits.end();
         it != end; it++) {
        os << *it << " ";
    }
    os << "0";

    return os;
}

}

#endif //_CLAUSES_H_
