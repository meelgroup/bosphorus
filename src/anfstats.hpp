/*****************************************************************************
Copyright (C) 2026  Mate Soos

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

#pragma once

#include <cstdint>
#include <string>
#include "configdata.hpp"

namespace BLib {

class ANF;

/// A snapshot of the size of an ANF, taken before and after every rewrite
/// rule so the effect of the rule can be printed as a coloured diff.
struct ANFStats {
    uint64_t eqs = 0;        ///< number of equations
    uint64_t monoms = 0;     ///< total number of monomials over all equations
    uint64_t lin_eqs = 0;    ///< equations of degree 1 (XORs)
    uint64_t nonlin_eqs = 0; ///< equations of degree >= 2
    uint64_t max_deg = 0;    ///< maximum degree over all equations
    uint64_t free_vars = 0;  ///< variables neither set nor replaced
    uint64_t set_vars = 0;   ///< variables with a fixed value
    uint64_t repl_vars = 0;  ///< variables replaced by another (anti-)equivalent one
    uint64_t mem_mb = 0;     ///< resident memory in MB
    double time = 0;         ///< CPU time when the snapshot was taken
};

/// Whether "c [simp-stats]" lines should be coloured, from config.color
/// (0 = never, 1 = always, 2 = only when stdout is a terminal and NO_COLOR is
/// not set in the environment).
bool stats_use_color(const ConfigData& config);

/// Prints one "bef"/"aft" stats block. `prev` is the snapshot taken before
/// the rule ran, so that every number can be coloured green when it went
/// down and red when it went up; it is nullptr for the "bef" block.
void print_simp_stats(const ConfigData& config, const char* when,
                      const std::string& rule, const ANFStats& cur,
                      const ANFStats* prev, unsigned depth);

/// RAII helper: prints the stats of `anf` labelled "bef <rule>" when created
/// and "aft <rule>" (with the diff coloured) when destroyed. Every rewrite
/// rule simply creates one of these at its top, so the printing is uniform:
///
///     SimpStatsScope scope(*this, "mono-red");
///
/// Scopes nest; nested ones print with a larger depth and are only shown
/// at verbosity >= 2.
class SimpStatsScope
{
   public:
    SimpStatsScope(ANF& anf, const char* rule);
    ~SimpStatsScope();
    SimpStatsScope(const SimpStatsScope&) = delete;
    SimpStatsScope& operator=(const SimpStatsScope&) = delete;

   private:
    ANF& anf;
    std::string rule;
    ANFStats bef;
    unsigned depth;
    bool active;
};

}
