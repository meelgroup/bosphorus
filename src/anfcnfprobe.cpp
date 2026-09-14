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

// cnf-probe: CryptoMiniSat's inprocessing as a rewrite rule, the way Arjun
// runs it (puura / Minimize::simplify): the ANF is converted to CNF, the
// solver runs equivalent-literal substitution (SCC), probes every variable
// that stands for an ANF variable (probe_all: failed literals, and the
// implications both branches agree on) and in-tree probing, and every
// literal fixed at decision level 0 and every binary XOR (equivalent
// literals) is translated back: a CNF variable stands for a monomial, a
// partial XOR or a lineral, so a unit is "that polynomial = c" and a binary
// XOR "p1 + p2 + c = 0". No variable elimination runs, so every variable
// keeps its meaning. Everything found is implied by the CNF, which is
// equivalent to the ANF, so it is sound to add.

#include <algorithm>
#include <iomanip>

#include "anf.hpp"
#include "cnf.hpp"
#include "time_mem.h"
#include "cryptominisat5/cryptominisat.h"

using std::cout;
using std::endl;
using namespace BLib;

size_t ANF::cnf_probe()
{
    SimpStatsScope scope(*this, "cnf-probe");
    const double myTime = cpuTime();
    if (eqs.empty() || !getOK()) return 0;

    // plain clauses (XORs cut, no partner variables): the solver's
    // inprocessing works on clauses, and the meaning of every variable is
    // a monomial, a cut of an XOR or a lineral
    ConfigData cconf = config;
    cconf.doPartner = false;
    cconf.xorClauses = false;
    cconf.verbosity = 0;
    cconf.writecomments = true; // the meaning of every XOR-cut variable is built only then
    CNF cnf(*this, cconf);

    CMSat::SATSolver solver;
    solver.set_verbosity(config.verbosity >= 4 ? 1 : 0);
    solver.set_num_threads(config.numThreads);
    solver.set_bve(0);        // no elimination: every variable keeps its meaning
    solver.set_renumber(false);
    solver.set_scc(1);
    solver.set_intree_probe(1);
    solver.new_vars(cnf.getNumVars());
    size_t num_cls = 0;
    for (const auto& cs : cnf.getClauses()) {
        for (const Clause& c : cs.first) {
            vector<Lit> lits = c.getClause();
            solver.add_clause(*(vector<CMSat::Lit>*)&lits);
            num_cls++;
        }
    }

    // the variables of the ANF, most incident first, are probed
    std::string s = "clean-cls, must-scc-vrepl";
    CMSat::lbool ret = solver.simplify(nullptr, &s);
    size_t probed = 0;
    if (ret != CMSat::l_False) {
        vector<uint32_t> ord;
        for (uint32_t v = 0; v < cnf.getNumVars(); v++) {
            if (cnf.getVarKind(v) == CNF::kind_var) ord.push_back(v);
        }
        const vector<uint32_t> inc = solver.get_var_incidence();
        std::stable_sort(ord.begin(), ord.end(), [&](uint32_t a, uint32_t b) {
            return (a < inc.size() ? inc[a] : 0) > (b < inc.size() ? inc[b] : 0);
        });
        if (ord.size() > config.cnfProbeVars) ord.resize(config.cnfProbeVars);
        probed = ord.size();
        if (!ord.empty()) ret = solver.probe_all(ord);
    }
    if (ret != CMSat::l_False) {
        s = "must-scc-vrepl, intree-probe, must-scc-vrepl, sub-impl";
        ret = solver.simplify(nullptr, &s);
    }

    // back to the ANF. A variable whose meaning is not a polynomial with
    // variables (never the case with the comments on, but never rely on
    // it: a wrong meaning gives a wrong equation) is not used.
    auto poly_of_var = [&](uint32_t v) -> BoolePolynomial {
        if (cnf.varRepresentsMonomial(v)) return BoolePolynomial(cnf.getMonomForVar(v));
        return cnf.getPolyForVar(v);
    };
    auto known = [&](uint32_t v) {
        if (v >= cnf.getNumVars()) return false;
        if (cnf.varRepresentsMonomial(v)) return true;
        return !cnf.getPolyForVar(v).isConstant();
    };
    vector<BoolePolynomial> facts;
    size_t num_units = 0, num_equiv = 0, num_long = 0;
    // a long nonlinear fact (the difference of two XOR cuts of linearised
    // equations) is a combination of equations already there: dropped
    auto keep = [&](const BoolePolynomial& p) {
        if (p.deg() <= 1 || p.length() <= config.cnfProbeLen) { facts.push_back(p); return true; }
        num_long++;
        return false;
    };
    if (ret == CMSat::l_False) {
        facts.push_back(BoolePolynomial(true, *ring));
    } else {
        for (const CMSat::Lit& l : solver.get_zero_assigned_lits()) {
            if (!known(l.var())) continue;
            BoolePolynomial p = poly_of_var(l.var());
            p += BooleConstant(!l.sign()); // the variable is true: p + 1 = 0
            if (config.verbosity >= 5) cout << "c [cnf-probe] unit " << l << " -> " << p << endl;
            if (keep(p)) num_units++;
        }
        for (const auto& pr : solver.get_all_binary_xors()) {
            const uint32_t v1 = pr.first.var(), v2 = pr.second.var();
            if (!known(v1) || !known(v2) || v1 == v2) continue;
            BoolePolynomial p = poly_of_var(v1) + poly_of_var(v2)
                                + BooleConstant(pr.first.sign() ^ pr.second.sign());
            if (config.verbosity >= 5) cout << "c [cnf-probe] equiv " << pr.first << " " << pr.second << " -> " << p << endl;
            if (p.isConstant()) { if (p.isOne()) facts.push_back(p); continue; }
            if (keep(p)) num_equiv++;
        }
    }
    // The CNF carries the values and equivalences the replacer already
    // knows, and the solver reports them back: contextualising the facts
    // turns those into 0, so only what is new is counted and added; linear
    // facts in the span of the linear equations are dropped as well.
    const size_t added = add_linearly_new_facts(facts, true);
    if (added > 0) {
        if (!propagate()) setNOTOK();
    }
    if (config.verbosity >= 1) {
        cout << "c [cnf-probe] cnf vars " << cnf.getNumVars() << " cls " << num_cls
             << " probed " << probed << " units " << num_units << " equivs " << num_equiv
             << " long-dropped " << num_long << " new " << added << (ret == CMSat::l_False ? " UNSAT" : "") << " T: " << std::fixed
             << std::setprecision(2) << (cpuTime() - myTime) << endl;
    }
    return added;
}
