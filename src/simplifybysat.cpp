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

#include "simplifybysat.hpp"
#include "cryptominisat5/cryptominisat.h"
#include "time_mem.h"

using std::cout;
using std::endl;

using namespace BosphLib;

inline bool testSolution(const ANF& anf, const vector<lbool>& solution)
{
    bool goodSol = anf.evaluate(solution);
    if (!goodSol) {
        cout << "ERROR! Solution found is incorrect!" << endl;
        exit(-1);
    }
    return goodSol;
}

SimplifyBySat::SimplifyBySat(const CNF& _cnf, const ConfigData& _config)
    : config(_config), cnf(_cnf)
{
    // Create SAT solver
    solver = new CMSat::SATSolver();
    solver->set_verbosity(config.verbosity >= 5 ? 1 : 0);
}

SimplifyBySat::~SimplifyBySat()
{
    delete solver;
}

void SimplifyBySat::addClausesToSolver(size_t beg)
{
    const auto& csets = cnf.getClauses();
    for (auto it = csets.begin() + beg; it != csets.end(); ++it) {
        for (const Clause& c : it->first) {
            const vector<Lit>& lits = c.getClause();
            solver->add_clause(lits);
        }
    }
}

int SimplifyBySat::extractUnitaries(vector<BoolePolynomial>& loop_learnt)
{
    vector<Lit> units = solver->get_zero_assigned_lits();
    if (config.verbosity >= 3) {
        cout << units.size(); // "c Number of unit learnt clauses: " << << endl;
    }

    uint64_t numVarLearnt = 0;
    for (const Lit& unit : units) {
        //If var represents a partial XOR clause, skip it
        if (!cnf.varRepresentsMonomial(unit.var())) {
            continue;
        }

        BooleMonomial m = cnf.getMonomForVar(unit.var());
        assert(m.deg() > 0);

        // Monomial is high degree, and FALSE. That doesn't help much
        if (m.deg() > 1 && unit.sign() == true) {
            continue;
        }

        // If DEG is 1, then this will set the variable
        // If DEG is >0 and setting is TRUE, the addBoolePolynomial() will
        // automatically set all variables in the monomial to ONE
        BoolePolynomial poly(!unit.sign(), cnf.getANFRing());
        poly += m;

        loop_learnt.push_back(poly);
        ++numVarLearnt;
    }

    if (config.verbosity >= 3) {
        cout << '/'
             << numVarLearnt; // "c Num ANF assignments learnt: " <<  << endl;
    }
    return numVarLearnt;
}

int SimplifyBySat::extractBinaries(vector<BoolePolynomial>& loop_learnt)
{
    vector<pair<Lit, Lit> > binXors = solver->get_all_binary_xors();
    if (config.verbosity >= 3) {
        cout << '/'
             << binXors.size(); //"c Number of binary clauses:" <<  << endl;
    }

    uint64_t numVarReplaced = 0;
    for (pair<Lit, Lit>& pair : binXors) {
        Lit lit1 = pair.first;
        Lit lit2 = pair.second;
        uint32_t v1 = lit1.var();
        uint32_t v2 = lit2.var();
        assert(v1 != v2);

        //If any of the two vars represent a partial XOR clause, skip it
        if (!cnf.varRepresentsMonomial(v1) || !cnf.varRepresentsMonomial(v2)) {
            continue;
        }

        BooleMonomial m1 = cnf.getMonomForVar(v1);
        BooleMonomial m2 = cnf.getMonomForVar(v2);

        // Not anti/equivalence
        if (m1.deg() > 1 || m2.deg() > 1) {
            continue;
        }

        BoolePolynomial poly((lit1.sign() ^ lit2.sign()), cnf.getANFRing());
        poly += m1;
        poly += m2;

        loop_learnt.push_back(poly);
        ++numVarReplaced;
    }

    if (config.verbosity >= 3) {
        cout
            << '/'
            << numVarReplaced; //"c Num ANF anti/equivalence learnt: " <<  << endl;
    }
    return numVarReplaced;
}

bool SimplifyBySat::addPolynomial(vector<BoolePolynomial>& loop_learnt,
                                  const pair<vector<uint32_t>, bool>& cnf_poly)
{
    BoolePolynomial new_poly(cnf_poly.second, cnf.getANFRing());
    for (const uint32_t& var_idx : cnf_poly.first) {
        if (!cnf.varRepresentsMonomial(var_idx)) {
            return false;
        }
        new_poly += cnf.getMonomForVar(var_idx);
    }

    if (new_poly.deg() == 1) {
        loop_learnt.push_back(new_poly);
        return true;
    }
    return false;
}

int SimplifyBySat::process(
    vector<BoolePolynomial>& loop_learnt,
    const vector<pair<vector<uint32_t>, bool> >& extracted)
{
    int num_polys = 0;
    for (const pair<vector<uint32_t>, bool>& cnf_poly : extracted) {
        num_polys += addPolynomial(loop_learnt, cnf_poly);
    }
    return num_polys;
}

int SimplifyBySat::extractLinear(vector<BoolePolynomial>& loop_learnt)
{
    int num_polys = 0;
    num_polys += process(loop_learnt, solver->get_recovered_xors(false));
    num_polys += process(loop_learnt, solver->get_recovered_xors(true));

    if (config.verbosity >= 3) {
        cout << '/' << num_polys; //"c Num ANF linear equations learnt: "
    }
    return num_polys;
}

int SimplifyBySat::simplify(const uint64_t numConfl_lim,
                            const uint64_t numConflinc, const double time_limit,
                            const size_t cbeg,
                            vector<BoolePolynomial>& loop_learnt, ANF& anf,
                            Solution& solution)
{
    if (!anf.getOK()) {
        cout << "c Nothing to simplify: UNSAT" << endl;
        return -1;
    }

    //Add problem to solver
    solver->new_vars(cnf.getNumVars() - solver->nVars());
    addClausesToSolver(cbeg);

    double time_left = time_limit - cpuTime();
    uint64_t numConfl_left = numConfl_lim;

    // Solve system of CNF until conflict limit
    if (config.verbosity >= 3) {
        cout << "c  Converted CNF has " << cnf.getNumVars() << " variables and "
             << cnf.getNumClauses() << " clause-sets to solve in increments of "
             << numConflinc << " conflicts until " << numConfl_lim
             << " conflicts within " << std::scientific << time_left
             << " seconds" << std::fixed << endl;
    }

    //Run the solver
    solver->set_max_time(time_left);
    solver->set_timeout_all_calls(time_left);
    solver->set_max_confl(std::min(numConflinc, numConfl_left));
    solution.ret = solver->solve();
    time_left = time_limit - cpuTime();
    numConfl_left =
        (numConfl_left > numConflinc) ? (numConfl_left - numConflinc) : 0;

    if (config.verbosity >= 4)
        cout << "c ... " << solver->get_sum_conflicts() << '/'
             << solver->get_sum_propagations() << '/'
             << solver->get_sum_decisions() << ' ' << std::scientific
             << time_left << std::fixed << " seconds" << endl;

    //Extract data
    const size_t prev_loop_learnt_sz = loop_learnt.size();
    if (config.verbosity >= 3) {
        cout << "c  Number of unit/assigns/binary/(anti-)equiv/linear ";
    }
    extractUnitaries(loop_learnt);
    extractBinaries(loop_learnt);
    extractLinear(loop_learnt);
    if (config.verbosity >= 3) {
        cout << endl;
    }

    //Deal with result
    if (solution.ret == l_Undef) {
        return 0;
    }

    if (solution.ret == l_False) {
        cout << "c UNSAT returned by solver" << endl;
        anf.addBoolePolynomial(BoolePolynomial(true, anf.getRing()));
        return -1;
    }

    //Solution found
    assert(solution.ret == l_True);
    if (config.verbosity >= 1) {
        cout << "c [SAT] has found a solution" << endl;
    }

    const size_t sz = solver->get_model().size();
    vector<lbool> solutionFromSolver(sz, l_Undef);
    for (uint32_t i = 0; i < sz; ++i) {
        solutionFromSolver[i] = solver->get_model()[i];
        assert(solutionFromSolver[i] != l_Undef);
    }

    vector<lbool> solution2 = cnf.mapSolToOrig(solutionFromSolver);
    solution.sol = anf.extendSolution(solution2);

    return 0;
}
