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
#include <map>
#include <unordered_set>

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
    bef = anf.get_stats(); // always: the totals per rule are kept at every verbosity
    if (!active) return;
    print_simp_stats(anf.get_config(), "bef", rule, bef, nullptr, depth);
}

SimpStatsScope::~SimpStatsScope()
{
    anf.stats_depth--;
    const ANFStats aft = anf.get_stats();
    RuleStats& rs = anf.rule_stats[rule];
    rs.calls++;
    rs.time += aft.time - bef.time;
    rs.eqs += (int64_t)aft.eqs - (int64_t)bef.eqs;
    rs.monoms += (int64_t)aft.monoms - (int64_t)bef.monoms;
    rs.lin_eqs += (int64_t)aft.lin_eqs - (int64_t)bef.lin_eqs;
    rs.set_vars += (int64_t)aft.set_vars - (int64_t)bef.set_vars;
    rs.repl_vars += (int64_t)aft.repl_vars - (int64_t)bef.repl_vars;
    if (aft.eqs != bef.eqs || aft.monoms != bef.monoms || aft.set_vars != bef.set_vars ||
        aft.repl_vars != bef.repl_vars || aft.lin_eqs != bef.lin_eqs) rs.effective++;
    if (!active) return;
    print_simp_stats(anf.get_config(), "aft", rule, aft, &bef, depth);
}

void ANF::printRuleStats() const
{
    if (rule_stats.empty()) return;
    cout << "c ---- rule stats (nested rules are included in the rule that called them) ----" << endl;
    cout << "c " << std::left << std::setw(14) << "rule" << std::right
         << std::setw(6) << "calls" << std::setw(6) << "eff" << std::setw(9) << "T"
         << std::setw(9) << "eqs" << std::setw(10) << "monoms" << std::setw(8) << "lin"
         << std::setw(7) << "set" << std::setw(7) << "repl" << std::setw(8) << "T/call" << endl;
    double total_time = 0;
    for (const auto& kv : rule_stats) {
        const RuleStats& r = kv.second;
        cout << "c " << std::left << std::setw(14) << kv.first << std::right
             << std::setw(6) << r.calls << std::setw(6) << r.effective
             << std::setw(9) << std::fixed << std::setprecision(2) << r.time
             << std::setw(9) << r.eqs << std::setw(10) << r.monoms << std::setw(8) << r.lin_eqs
             << std::setw(7) << r.set_vars << std::setw(7) << r.repl_vars
             << std::setw(8) << std::fixed << std::setprecision(3) << (r.calls ? r.time / r.calls : 0.0) << endl;
        total_time += r.time;
    }
    cout << "c ------------------------------------------------------------------------------" << endl;
}

// Density: how full the equations are and how much they share.
void ANF::printDensityStats() const
{
    if (eqs.empty()) return;
    uint64_t terms = 0, vars_sum = 0, max_len = 0, max_vars = 0;
    std::map<int, uint64_t> deg_hist;
    double fill_sum = 0;
    uint64_t fill_n = 0;
    std::unordered_set<uint64_t> distinct;
    // distinct monomials: over the polynomial equations only (a product of
    // long linerals has thousands of terms that are not worth expanding)
    for (size_t i = 0; i < eqs.size(); i++) {
        terms += eq_len[i];
        max_len = std::max<uint64_t>(max_len, eq_len[i]);
        const size_t nv = nVarsOf(i);
        vars_sum += nv;
        max_vars = std::max<uint64_t>(max_vars, nv);
        const int d = degOf(i);
        deg_hist[d]++;
        if (poly_valid[i]) {
            for (const BooleMonomial& t : eqs[i]) distinct.insert(t.stableHash());
            // fill: terms / squarefree monomials of degree <= d over the
            // equation's own variables (1 = every possible monomial is there)
            if (d >= 1 && nv <= 40) {
                double possible = 0, c = 1;
                for (int k = 0; k <= d; k++) {
                    possible += c;
                    c = c * (double)(nv - k) / (double)(k + 1);
                }
                fill_sum += (double)eq_len[i] / possible;
                fill_n++;
            }
        }
    }
    uint64_t active = 0, occ_sum = 0, occ_max = 0;
    for (size_t v = 0; v < occur.size(); v++) {
        if (occur[v].empty()) continue;
        active++;
        occ_sum += occur[v].size();
        occ_max = std::max<uint64_t>(occ_max, occur[v].size());
    }
    cout << "c Density: terms/eq " << std::fixed << std::setprecision(1)
         << (double)terms / eqs.size() << " (max " << max_len << "), vars/eq "
         << (double)vars_sum / eqs.size() << " (max " << max_vars << "), fill "
         << std::setprecision(3) << (fill_n ? fill_sum / fill_n : 0.0)
         << ", distinct monoms " << distinct.size() << " (sharing "
         << std::setprecision(2) << (distinct.empty() ? 0.0 : (double)terms / distinct.size())
         << "x), active vars " << active << ", eqs/var " << std::setprecision(1)
         << (active ? (double)occ_sum / active : 0.0) << " (max " << occ_max << ")" << endl;
    cout << "c Degree histogram:";
    for (const auto& kv : deg_hist) cout << " deg" << kv.first << ":" << kv.second;
    cout << endl;
}
