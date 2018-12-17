/*****************************************************************************
Copyright (C) 2016  Mate Soos
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

#ifndef _ALG_TYPES_H
#define _ALG_TYPES_H

#include <cstdint>

static constexpr const uint32_t var_Undef = (0xffffffffU >> 4);

class Lit
{
    uint32_t x;
    explicit Lit(uint32_t i) : x(i)
    {
    }

   public:
    Lit() : x(var_Undef << 1)
    {
    } // (lit_Undef)
    explicit Lit(uint32_t var, bool is_inverted) : x(var + var + is_inverted)
    {
    }

    const uint32_t& toInt() const
    { // Guarantees small, positive integers suitable for array indexing.
        return x;
    }
    Lit operator~() const
    {
        return Lit(x ^ 1);
    }
    Lit operator^(const bool b) const
    {
        return Lit(x ^ (uint32_t)b);
    }
    Lit& operator^=(const bool b)
    {
        x ^= (uint32_t)b;
        return *this;
    }
    bool sign() const
    {
        return x & 1;
    }
    uint32_t var() const
    {
        return x >> 1;
    }
    Lit unsign() const
    {
        return Lit(x & ~1U);
    }
    bool operator==(const Lit& p) const
    {
        return x == p.x;
    }
    bool operator!=(const Lit& p) const
    {
        return x != p.x;
    }
    /**
    @brief ONLY to be used for ordering such as: a, b, ~b, etc.
    */
    bool operator<(const Lit& p) const
    {
        return x <
               p.x; // '<' guarantees that p, ~p are adjacent in the ordering.
    }
    bool operator>(const Lit& p) const
    {
        return x > p.x;
    }
    bool operator>=(const Lit& p) const
    {
        return x >= p.x;
    }
    static Lit toLit(uint32_t data)
    {
        return Lit(data);
    }
};

static const Lit lit_Undef(var_Undef, false); // Useful special constants.
static const Lit lit_Error(var_Undef, true);  //

inline std::ostream& operator<<(std::ostream& os, const Lit lit)
{
    if (lit == lit_Undef) {
        os << "lit_Undef";
    } else {
        os << (lit.sign() ? "-" : "") << (lit.var() + 1);
    }
    return os;
}

class lbool
{
    uint8_t value;

   public:
    explicit constexpr lbool(uint8_t v) : value(v)
    {
    }

    lbool() : value(0)
    {
    }
    explicit lbool(bool x) : value(!x)
    {
    }

    bool operator==(lbool b) const
    {
        return ((b.value & 2) & (value & 2)) |
               (!(b.value & 2) & (value == b.value));
    }
    bool operator!=(lbool b) const
    {
        return !(*this == b);
    }
    lbool operator^(bool b) const
    {
        return lbool((uint8_t)(value ^ (uint8_t)b));
    }

    lbool operator&&(lbool b) const
    {
        uint8_t sel = (this->value << 1) | (b.value << 3);
        uint8_t v = (0xF7F755F4 >> sel) & 3;
        return lbool(v);
    }

    lbool operator||(lbool b) const
    {
        uint8_t sel = (this->value << 1) | (b.value << 3);
        uint8_t v = (0xFCFCF400 >> sel) & 3;
        return lbool(v);
    }

    friend int toInt(lbool l);
    friend lbool toLbool(int v);
};

static constexpr const lbool l_True = lbool((uint8_t)0);
static constexpr const lbool l_False = lbool((uint8_t)1);
static constexpr const lbool l_Undef = lbool((uint8_t)2);

inline lbool boolToLBool(const bool b)
{
    if (b)
        return l_True;
    else
        return l_False;
}

inline std::ostream& operator<<(std::ostream& cout, const lbool val)
{
    if (val == l_True)
        cout << "l_True";
    if (val == l_False)
        cout << "l_False";
    if (val == l_Undef)
        cout << "l_Undef";
    return cout;
}

#endif //_ALG_TYPES_H
