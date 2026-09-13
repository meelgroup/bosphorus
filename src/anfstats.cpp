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

#include "anfstats.hpp"

#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>

#include "anf.hpp"
#include "time_mem.h"

using std::cout;
using std::endl;
using namespace BLib;

bool BLib::stats_use_color(const ConfigData& config)
{
    if (config.color == 0) return false;
    if (config.color == 1) return true;
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return isatty(fileno(stdout));
}

namespace {

const char* const COL_RULE = "\033[38;5;208m"; // orange
const char* const COL_DOWN = "\033[32m";       // green
const char* const COL_UP = "\033[31m";         // red
const char* const COL_RESET = "\033[0m";

struct Field {
    const char* name;
    uint64_t ANFStats::*member;
};

// The layout mirrors CryptoMiniSat's "[simp-stats]" lines: the sizes of the
// system on the first line, the variable bookkeeping and timing on the second.
const Field line1[] = {
    {"eqs", &ANFStats::eqs},
    {"monoms", &ANFStats::monoms},
    {"lin_eqs", &ANFStats::lin_eqs},
    {"nonlin_eqs", &ANFStats::nonlin_eqs},
    {"max_deg", &ANFStats::max_deg},
};
const Field line2[] = {
    {"free_vars", &ANFStats::free_vars},
    {"set_vars", &ANFStats::set_vars},
    {"repl_vars", &ANFStats::repl_vars},
    {"mem_MB", &ANFStats::mem_mb},
};

void print_fields(std::ostream& os, const Field* fields, size_t n,
                  const ANFStats& cur, const ANFStats* prev, bool color)
{
    for (size_t i = 0; i < n; i++) {
        const uint64_t v = cur.*(fields[i].member);
        os << ' ' << fields[i].name << ' ';
        const char* col = nullptr;
        if (prev != nullptr && color) {
            const uint64_t p = (*prev).*(fields[i].member);
            if (v < p) col = COL_DOWN;
            else if (v > p) col = COL_UP;
        }
        if (col) os << col;
        os << v;
        if (col) os << COL_RESET;
    }
}

}

void BLib::print_simp_stats(const ConfigData& config, const char* when,
                            const std::string& rule, const ANFStats& cur,
                            const ANFStats* prev, unsigned depth)
{
    const bool color = stats_use_color(config);
    const std::string indent(depth * 2, ' ');

    // Pad the rule name so the numbers line up between "bef" and "aft".
    std::ostringstream head;
    head << "c [simp-stats] " << indent << when << ' ';
    std::ostringstream rule_os;
    rule_os << std::left << std::setw(22) << rule;

    cout << head.str();
    if (color) cout << COL_RULE;
    cout << rule_os.str();
    if (color) cout << COL_RESET;
    print_fields(cout, line1, sizeof(line1) / sizeof(line1[0]), cur, prev,
                 color);
    cout << endl;

    cout << "c [simp-stats] " << indent
         << std::string(std::string(when).size() + 1 + 22, ' ');
    print_fields(cout, line2, sizeof(line2) / sizeof(line2[0]), cur, prev,
                 color);
    cout << " T: " << std::fixed << std::setprecision(2) << cur.time;
    if (prev != nullptr) {
        cout << " T-step: " << std::fixed << std::setprecision(2)
             << (cur.time - prev->time);
    }
    cout << " depth " << depth << endl;
}

SimpStatsScope::SimpStatsScope(ANF& _anf, const char* _rule)
    : anf(_anf), rule(_rule)
{
    depth = anf.stats_depth++;
    const uint32_t verb = anf.get_config().verbosity;
    active = (depth == 0) ? (verb >= 1) : (verb >= 2);
    if (!active) return;
    bef = anf.get_stats();
    print_simp_stats(anf.get_config(), "bef", rule, bef, nullptr, depth);
}

SimpStatsScope::~SimpStatsScope()
{
    anf.stats_depth--;
    if (!active) return;
    const ANFStats aft = anf.get_stats();
    print_simp_stats(anf.get_config(), "aft", rule, aft, &bef, depth);
}
