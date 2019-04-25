/*****************************************************************************
Copyright (C) 2016  Security Research Labs
Copyright (C) 2018  Davin Choo, Kian Ming A. Chai, DSO National Laboratories
Copyright (C) 2018  Mate Soos

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

#include "bosphorus.h"

#include "elimlin.hpp"
#include "extendedlinearization.hpp"

#ifdef SAT_SIMP
#include "simplifybysat.hpp"
#endif

Library::~Library()
{
    delete polybori_ring;
}

void Library::check_library_in_use()
{
    if (read_in_data) {
        cout << "ERROR: data already read in."
             << " You can only read in *one* ANF or CNF per library creation"
             << endl;
        exit(-1);
    }
    read_in_data = true;

    assert(extra_clauses.empty());
    //     assert(anf == nullptr);
    //     assert(cnf = nullptr);
    assert(polybori_ring == nullptr);
}

ANF* Library::read_anf(const char* fname)
{
    check_library_in_use();

    // Find out maxVar in input ANF file
    size_t maxVar = ANF::readFileForMaxVar(fname);

    // Construct ANF
    // ring size = maxVar + 1, because ANF variables start from x0
    polybori_ring = new BoolePolyRing(maxVar + 1);
    ANF* anf = new ANF(polybori_ring, config);
    anf->readFile(fname);
    return anf;
}

ANF* Library::start_cnf_input(uint32_t max_vars)
{
    polybori_ring = new BoolePolyRing(max_vars);
    ANF* anf = new ANF(polybori_ring, config);
    return anf;
}

void Library::add_clause(ANF* anf, const std::vector<int>& clause)
{
    BoolePolynomial poly(1, *polybori_ring);
    for (const int lit: clause) {
        assert(lit != 0);
        BoolePolynomial alsoAdd(*polybori_ring);
        if (lit < 0)
            alsoAdd = poly;
        poly *= BooleVariable(std::abs(lit)-1, *polybori_ring);
        poly += alsoAdd;
    }
    anf->addBoolePolynomial(poly);
}


ANF* Library::read_cnf(const char* fname)
{
    check_library_in_use();

    DIMACSCache dimacs_cache(fname);
    const vector<Clause>& orig_clauses(dimacs_cache.getClauses());
    size_t maxVar = dimacs_cache.getMaxVar();

    // Chunk up by L positive literals. L = config.cutNum
    vector<Clause> chunked_clauses;
    for (auto clause : orig_clauses) {
        // small already
        if (clause.size() <= config.cutNum) {
            chunked_clauses.push_back(clause);
            continue;
        }

        // Count number of positive literals
        size_t positive_count = 0;
        for (const Lit& l : clause.getLits()) {
            positive_count += !l.sign();
        }
        if (positive_count <= config.cutNum) {
            chunked_clauses.push_back(clause);
            continue;
        }

        // need to chop it up
        if (config.verbosity >= 5)
            cout << clause << " --> ";

        vector<Lit> collect;
        size_t count = 0;
        for (auto l : clause.getLits()) {
            collect.push_back(l);
            count += !l.sign();
            if (count > config.cutNum) {
                // Create new aux
                Lit aux = Lit(maxVar, false);
                maxVar++;

                // replace the last one when a negative auxilary
                Lit prev = collect.back();
                collect.back() = ~aux;

                extra_clauses.push_back(collect);
                chunked_clauses.push_back(collect);
                if (config.verbosity >= 5)
                    cout << collect << " and ";

                collect.clear();
                collect.push_back(aux);
                collect.push_back(prev);
                count = 2; // because both aux and prev are positives!
            }
        } //idx
        if (!collect.empty()) {
            extra_clauses.push_back(collect);
            chunked_clauses.push_back(collect);
            if (config.verbosity >= 5)
                cout << collect;
        }
        if (config.verbosity >= 5)
            cout << endl;
    } //for

    // Construct ANF
    if (config.verbosity >= 4) {
        cout << "c Constructing CNF with " << maxVar << " variables." << endl;
    }
    // ring size = maxVar, because CNF variables start from 1
    polybori_ring = new BoolePolyRing(maxVar);
    ANF* anf = new ANF(polybori_ring, config);
    for (auto clause : chunked_clauses) {
        BoolePolynomial poly(1, *polybori_ring);
        for (const Lit& l : clause.getLits()) {
            BoolePolynomial alsoAdd(*polybori_ring);
            if (!l.sign())
                alsoAdd = poly;
            poly *= BooleVariable(l.var(), *polybori_ring);
            poly += alsoAdd;
        }
        anf->addBoolePolynomial(poly);
        if (config.verbosity >= 5) {
            cout << clause << " -> " << poly << endl;
        }
    }

    return anf;
}

void Library::write_anf(const char* fname, const ANF* anf)
{
    std::ofstream ofs;
    ofs.open(fname);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << fname << "\" for writing\n";
        exit(-1);
    } else {
        ofs << "c Executed arguments: " << config.executedArgs << endl;
        ofs << *anf << endl;
    }
    ofs.close();
}

void Library::write_cnf(const char* input_cnf_fname,
                        const char* output_cnf_fname, const ANF* anf)
{
    CNF* cnf = NULL;
    if (input_cnf_fname == NULL) {
        cnf = cnf_from_anf_and_cnf(input_cnf_fname, anf);
    } else {
        cnf = anf_to_cnf(anf);
    }

    std::ofstream ofs;
    ofs.open(output_cnf_fname);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << output_cnf_fname
                  << "\" for writing\n";
        exit(-1);
    } else {
        if (config.writecomments) {
            ofs << "c Executed arguments: " << config.executedArgs << endl;
            for (size_t i = 0; i < anf->getRing().nVariables(); i++) {
                Lit l = anf->getReplaced(i);
                BooleVariable v(l.var(), anf->getRing());
                if (l.sign()) {
                    ofs << "c MAP " << i + 1 << " = -" << cnf->getVarForMonom(v)
                        << endl;
                } else {
                    ofs << "c MAP " << i + 1 << " = " << cnf->getVarForMonom(v)
                        << endl;
                }
            }
            for (size_t i = 0; i < cnf->getNumVars(); ++i) {
                const BooleMonomial mono = cnf->getMonomForVar(i);
                if (mono.deg() > 0)
                    assert(i == cnf->getVarForMonom(mono));
                if (mono.deg() > 1)
                    ofs << "c MAP " << i + 1 << " = " << mono << endl;
            }
        }

        cnf->print_without_header(ofs);

        ofs << "c Learnt " << learnt.size() << " fact(s)\n";
        if (config.writecomments) {
            for (const BoolePolynomial& poly : learnt) {
                ofs << "c " << poly << endl;
            }
        }
    }
    ofs.close();
}

CNF* Library::anf_to_cnf(const ANF* anf)
{
    double convStartTime = cpuTime();
    CNF* cnf = new CNF(*anf, config);
    if (config.verbosity >= 2) {
        cout << "c [CNF conversion] in " << (cpuTime() - convStartTime)
             << " seconds.\n";
        cnf->printStats();
    }
    return cnf;
}

CNF* Library::cnf_from_anf_and_cnf(const char* cnf_fname, const ANF* anf)
{
    double convStartTime = cpuTime();
    CNF* cnf = new CNF(cnf_fname, *anf, extra_clauses, config);
    if (config.verbosity >= 2) {
        cout << "c [CNF enhancing] in " << (cpuTime() - convStartTime)
             << " seconds.\n";
        cnf->printStats();
    }
    return cnf;
}

Solution Library::simplify(ANF* anf, const char* orig_cnf_file)
{
    cout << "c [boshp] Running iterative simplification..." << endl;
    bool timeout = (cpuTime() > config.maxTime);
    if (timeout) {
        if (config.verbosity) {
            cout << "c Timeout before learning" << endl;
        }
        return Solution();
    }

    double loopStartTime = cpuTime();
    // Perform initial propagation to avoid needing >= 2 iterations
    anf->propagate();
    timeout = (cpuTime() > config.maxTime);

    Solution solution;
    bool changes[] = {true, true, true}; // any changes for the strategies
    size_t waits[] = {0, 0, 0};
    size_t countdowns[] = {0, 0, 0};
    uint32_t iters = 0;
    unsigned subiter = 0;
    CNF* cnf = NULL;
    SimplifyBySat* sbs = NULL;

    while (
        !timeout
        && anf->getOK()
        && solution.ret == l_Undef
        && (changes[0] || changes[1] || changes[2] || iters < 3)
    ) {
        cout << "c [iter-simp] ------ Iteration " << std::fixed << std::dec
             << (int)iters << endl;

        static const char* strategy_str[] = {"XL", "ElimLin", "SAT"};
        const double startTime = cpuTime();
        int num_learnt = 0;

        if (countdowns[subiter] > 0) {
            cout << "c [" << strategy_str[subiter] << "] waiting for "
                 << countdowns[subiter] << " iteration(s)." << endl;
        } else {
            const size_t prevsz = learnt.size();
            switch (subiter) {
                case 0:
                    if (config.doXL) {
                        if (!extendedLinearization(config, anf->getEqs(),
                                                   learnt)) {
                            anf->setNOTOK();
                        } else {
                            for (size_t i = prevsz; i < learnt.size(); ++i)
                                num_learnt +=
                                    anf->addBoolePolynomial(learnt[i]);
                        }
                    }
                    break;
                case 1:
                    if (config.doEL) {
                        if (!elimLin(config, anf->getEqs(), learnt)) {
                            anf->setNOTOK();
                        } else {
                            for (size_t i = prevsz; i < learnt.size(); ++i)
                                num_learnt +=
                                    anf->addBoolePolynomial(learnt[i]);
                        }
                    }
                    break;
                case 2:
#ifdef SAT_SIMP
                    if (config.doSAT) {
                        size_t no_cls = 0;
                        if (orig_cnf_file) {
                            if (cnf == NULL) {
                                assert(sbs == NULL);
                                cnf = new CNF(orig_cnf_file, *anf,
                                              extra_clauses, config);
                                sbs = new SimplifyBySat(*cnf, config);
                            } else {
                                no_cls = cnf->update();
                            }
                        } else {
                            delete cnf;
                            delete sbs;
                            cnf = new CNF(*anf, config);
                            sbs = new SimplifyBySat(*cnf, config);
                        }

                        sbs->simplify(config.numConfl_lim,
                                      config.numConfl_inc, config.maxTime,
                                      no_cls, learnt, *anf, solution);

                        if (solution.ret != l_False) {
                            for (size_t i = prevsz; i < learnt.size(); ++i) {
                                num_learnt += anf->addLearntBoolePolynomial(learnt[i]);
                            }
                        }
                    }
#endif
                    break;
            }

            if (config.verbosity >= 2) {
                cout << "c [" << strategy_str[subiter] << "] learnt "
                     << num_learnt << " new facts in "
                     << (cpuTime() - startTime) << " seconds." << endl;
            }
        }

        // Check if there are any changes to the system
        if (num_learnt <= 0) {
            changes[subiter] = false;
        } else {
            changes[subiter] = true;
            bool ok = anf->propagate();
            if (!ok) {
                if (config.verbosity >= 1)
                    cout << "c [ANF Propagation] is false\n";
                solution.ret = l_False;
            }
        }

        // Scheduling strategies
        if (changes[subiter]) {
            waits[subiter] = 0;
        } else {
            if (countdowns[subiter] > 0)
                --countdowns[subiter];
            else {
                static size_t series[] = {0, 1,  1,  2,  3,  5,
                                          8, 13, 21, 34, 55, 89};
                countdowns[subiter] = series[std::min(
                    waits[subiter], sizeof(series) / sizeof(series[0]) - 1)];
                ++waits[subiter];
            }
        }

        //Schedule next iteration
        if (subiter < 2) {
            ++subiter;
        } else {
            ++iters;
            subiter = 0;
            deduplicate(); // because this is a good time to do this
        }
        timeout = (cpuTime() > config.maxTime);
    }

    if (config.verbosity) {
        cout << "c [";
        if (timeout) {
            cout << "Timeout";
        }

        cout << " after " << iters << '.' << subiter
             << " iteration(s) in " << (cpuTime() - loopStartTime)
             << " seconds.]\n";
    }

    delete sbs;
    delete cnf;
    anf->contextualize(learnt);
    return solution;
}

void Library::deduplicate()
{
    vector<BoolePolynomial> dedup;
    ANF::eqs_hash_t hash;
    for (const BoolePolynomial& p : learnt) {
        if (hash.insert(p.hash()).second)
            dedup.push_back(p);
    }
    if (config.verbosity >= 3) {
        cout << "c [Dedup] " << learnt.size() << "->" << dedup.size() << endl;
    }
    learnt.swap(dedup);
}

void Library::add_trivial_learnt_from_anf_to_learnt(
    ANF* anf,
    const ANF::eqs_hash_t& orig_eqs_hash
) {
    // Add *NEW* assignments and equivalences
    for (uint32_t v = 0; v < anf->getRing().nVariables(); v++) {
        const lbool val = anf->value(v);
        const Lit lit = anf->getReplaced(v);
        BooleVariable bv = anf->getRing().variable(v);
        if (val != l_Undef) {
            BoolePolynomial assignment(bv + BooleConstant(val == l_True));
            if (orig_eqs_hash.find(assignment.hash()) == orig_eqs_hash.end())
                learnt.push_back(assignment);
        } else if (lit != Lit(v, false)) {
            BooleVariable bv2 = anf->getRing().variable(lit.var());
            BoolePolynomial equivalence(bv + bv2 + BooleConstant(lit.sign()));
            if (orig_eqs_hash.find(equivalence.hash()) == orig_eqs_hash.end())
                learnt.push_back(equivalence);
        }
    }
}

vector<Clause> Library::get_learnt(ANF* anf)
{
    vector<vector<int>> ret;
    ANF* anf2 = new ANF(&anf->getRing(), config);
    for(const auto& l: learnt) {
        anf2->addBoolePolynomial(l);
    }
    CNF* cnf = new CNF(*anf2, config);
    return cnf->get_clauses_simple();
}

void Library::set_config(const ConfigData& cfg)
{
    config = cfg;
}
