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

#include "bosphorus.hpp"

#include "GitSHA1.hpp"
#include "elimlin.hpp"
#include "extendedlinearization.hpp"
#include "dimacscache.hpp"
#include "gaussjordan.hpp"
#include "replacer.hpp"
#include "time_mem.h"
#include "bosphorus/solvertypesmini.hpp"
#include "elimlin.hpp"
#include "extendedlinearization.hpp"
#include "simplifybysat.hpp"
#include "anf.hpp"
#include "cnf.hpp"
#include "configdata.hpp"

#ifdef SAT_SIMP
#include "simplifybysat.hpp"
#endif

using Bosph::Lit;
using Bosph::Clause;
using Bosph::Solution;
using Bosph::lbool;
using Bosph::l_Undef;
using Bosph::l_True;
using Bosph::l_False;
using BLib::ConfigData;

class PrivateData {
public:
    ConfigData config;
    BoolePolyRing* pring = nullptr;
    vector<Clause> extra_clauses;
    vector<BoolePolynomial> learnt;

    bool read_in_data = false;
};

Bosphorus::Bosphorus()
{
    dat = new PrivateData;
}

Bosphorus::~Bosphorus()
{
    delete dat->pring;
    delete dat;
}

void Bosphorus::check_library_in_use()
{
    if (dat->read_in_data) {
        cout << "ERROR: data already read in."
             << " You can only read in *one* ANF or CNF per library creation"
             << endl;
        exit(-1);
    }
    dat->read_in_data = true;

    assert(dat->extra_clauses.empty());
    //     assert(anf == nullptr);
    //     assert(cnf = nullptr);
    assert(dat->pring == nullptr);
}

::ANF* Bosphorus::read_anf(const char* fname)
{
    check_library_in_use();

    // Find out maxVar in input ANF file
    size_t maxVar = BLib::ANF::readFileForMaxVar(fname);

    // Construct ANF
    // ring size = maxVar + 1, because ANF variables start from x0
    dat->pring = new BoolePolyRing(maxVar + 1);
    auto anf = new BLib::ANF(dat->pring, dat->config);
    anf->readFile(fname);
    return (ANF*)anf;
}

::ANF* Bosphorus::start_cnf_input(uint32_t max_vars)
{
    dat->pring = new BoolePolyRing(max_vars);
    auto anf = new BLib::ANF(dat->pring, dat->config);
    return (ANF*)anf;
}

void Bosphorus::add_clause(ANF* anf, const std::vector<int>& clause)
{
    BoolePolynomial poly(1, *dat->pring);
    for (const int lit: clause) {
        assert(lit != 0);
        BoolePolynomial alsoAdd(*dat->pring);
        if (lit < 0)
            alsoAdd = poly;
        poly *= BooleVariable(std::abs(lit)-1, *dat->pring);
        poly += alsoAdd;
    }
    ((BLib::ANF*)anf)->addBoolePolynomial(poly);
}


::ANF* Bosphorus::read_cnf(const char* fname)
{
    check_library_in_use();

    BLib::DIMACSCache dimacs_cache(fname);
    const vector<Clause>& orig_clauses(dimacs_cache.getClauses());
    size_t maxVar = dimacs_cache.getMaxVar();

    // Chunk up by L positive literals. L = config.cutNum
    vector<Clause> chunked_clauses;
    for (auto clause : orig_clauses) {
        // small already
        if (clause.size() <= dat->config.cutNum) {
            chunked_clauses.push_back(clause);
            continue;
        }

        // Count number of positive literals
        size_t positive_count = 0;
        for (const Lit& l : clause.getLits()) {
            positive_count += !l.sign();
        }
        if (positive_count <= dat->config.cutNum) {
            chunked_clauses.push_back(clause);
            continue;
        }

        // need to chop it up
        if (dat->config.verbosity >= 5)
            cout << clause << " --> ";

        vector<Lit> collect;
        size_t count = 0;
        for (auto l : clause.getLits()) {
            collect.push_back(l);
            count += !l.sign();
            if (count > dat->config.cutNum) {
                // Create new aux
                Lit aux = Lit(maxVar, false);
                maxVar++;

                // replace the last one when a negative auxilary
                Lit prev = collect.back();
                collect.back() = ~aux;

                dat->extra_clauses.push_back(collect);
                chunked_clauses.push_back(collect);
                if (dat->config.verbosity >= 5)
                    cout << collect << " and ";

                collect.clear();
                collect.push_back(aux);
                collect.push_back(prev);
                count = 2; // because both aux and prev are positives!
            }
        } //idx
        if (!collect.empty()) {
            dat->extra_clauses.push_back(collect);
            chunked_clauses.push_back(collect);
            if (dat->config.verbosity >= 5)
                cout << collect;
        }
        if (dat->config.verbosity >= 5)
            cout << endl;
    } //for

    // Construct ANF
    if (dat->config.verbosity >= 4) {
        cout << "c Constructing CNF with " << maxVar << " variables." << endl;
    }
    // ring size = maxVar, because CNF variables start from 1
    dat->pring = new BoolePolyRing(maxVar);
    auto anf = new BLib::ANF(dat->pring, dat->config);
    for (auto clause : chunked_clauses) {
        BoolePolynomial poly(1, *dat->pring);
        for (const Lit& l : clause.getLits()) {
            BoolePolynomial alsoAdd(*dat->pring);
            if (!l.sign())
                alsoAdd = poly;
            poly *= BooleVariable(l.var(), *dat->pring);
            poly += alsoAdd;
        }
        anf->addBoolePolynomial(poly);
        if (dat->config.verbosity >= 5) {
            cout << clause << " -> " << poly << endl;
        }
    }

    return (ANF*)anf;
}

void Bosphorus::write_anf(const char* fname, const ANF* anf)
{
    std::ofstream ofs;
    ofs.open(fname);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << fname << "\" for writing\n";
        exit(-1);
    } else {
        ofs << "c Executed arguments: " << dat->config.executedArgs << endl;
        ofs << *((BLib::ANF*)anf) << endl;
    }
    ofs.close();
}

CNF* Bosphorus::write_cnf(const char* input_cnf_fname,
                        const char* output_cnf_fname, const ::ANF* a)
{
    auto anf = (const BLib::ANF*)a;

    BLib::CNF* cnf = NULL;
    if (input_cnf_fname != NULL) {
        cnf = (BLib::CNF*)cnf_from_anf_and_cnf(input_cnf_fname, (ANF*)anf);
    } else {
        cnf = (BLib::CNF*)anf_to_cnf((ANF*)anf);
    }

    std::ofstream ofs;
    ofs.open(output_cnf_fname);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << output_cnf_fname
                  << "\" for writing\n";
        exit(-1);
    } else {
        if (dat->config.writecomments) {
            ofs << "c Executed arguments: " << dat->config.executedArgs << endl;
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

        ofs << "c Learnt " << dat->learnt.size() << " fact(s)\n";
        if (dat->config.writecomments) {
            for (const BoolePolynomial& poly : dat->learnt) {
                ofs << "c " << poly << endl;
            }
        }
    }
    ofs.close();

    return (CNF*)cnf;
}

void Bosphorus::write_solution_map(CNF* c, std::ofstream* ofs)
{
    auto cnf = (BLib::CNF*)c;
    cnf->print_solution_map(ofs);
}

void Bosphorus::write_solution_map(ANF* a, std::ofstream* ofs)
{
    auto anf = (BLib::ANF*)a;
    anf->print_solution_map(ofs);
}

CNF* Bosphorus::anf_to_cnf(const ANF* a)
{
    auto anf = (BLib::ANF*)a;

    double convStartTime = cpuTime();
    auto cnf = new BLib::CNF(*anf, dat->config);
    if (dat->config.verbosity >= 2) {
        cout << "c [CNF conversion] in " << (cpuTime() - convStartTime)
             << " seconds.\n";
        cnf->printStats();
    }
    return (CNF*)cnf;
}

CNF* Bosphorus::cnf_from_anf_and_cnf(const char* cnf_fname, const ANF* a)
{
    auto anf = (BLib::ANF*)a;

    double convStartTime = cpuTime();
    auto cnf = new BLib::CNF(cnf_fname, *anf, dat->extra_clauses, dat->config);
    if (dat->config.verbosity >= 2) {
        cout << "c [CNF enhancing] in " << (cpuTime() - convStartTime)
             << " seconds.\n";
        cnf->printStats();
    }
    return (CNF*)cnf;
}

bool Bosphorus::simplify(ANF* a, const char* orig_cnf_file, uint32_t max_iters)
{
    auto anf = (BLib::ANF*)a;

    cout << "c [boshp] Running iterative simplification..." << endl;
    bool timeout = (cpuTime() > dat->config.maxTime);
    if (timeout) {
        if (dat->config.verbosity) {
            cout << "c Timeout before learning" << endl;
        }
        return anf->getOK();
    }

    double loopStartTime = cpuTime();
    // Perform initial propagation to avoid needing >= 2 iterations
    if (!anf->propagate()) {
        cout << "c ANF is UNSAT after propagation" << endl;
        return false;
    }
    timeout = (cpuTime() > dat->config.maxTime);

    bool changes[] = {true, true, true}; // any changes for the strategies
    size_t waits[] = {0, 0, 0};
    size_t countdowns[] = {0, 0, 0};
    uint32_t iters = 0;
    unsigned subiter = 0;
    BLib::CNF* cnf = NULL;
    BLib::SimplifyBySat* sbs = NULL;

    while (
        !timeout
        && anf->getOK()
        && iters < max_iters
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
            const size_t prevsz = dat->learnt.size();
            bool sub_iter_performed = false;
            switch (subiter) {
                case 0:
                    if (dat->config.doXL) {
                        sub_iter_performed = true;
                        if (!extendedLinearization(dat->config, anf->getEqs(),
                                                   dat->learnt)) {
                            anf->setNOTOK();
                        } else {
                            for (size_t i = prevsz; i < dat->learnt.size(); ++i)
                                num_learnt +=
                                    anf->addBoolePolynomial(dat->learnt[i]);
                        }
                    }
                    break;
                case 1:
                    if (dat->config.doEL) {
                        sub_iter_performed = true;
                        if (!elimLin(dat->config, anf->getEqs(), dat->learnt)) {
                            anf->setNOTOK();
                        } else {
                            for (size_t i = prevsz; i < dat->learnt.size(); ++i)
                                num_learnt +=
                                    anf->addBoolePolynomial(dat->learnt[i]);
                        }
                    }
                    break;
                case 2:
#ifdef SAT_SIMP
                    if (dat->config.doSAT) {
                        sub_iter_performed = true;
                        size_t no_cls = 0;
                        if (orig_cnf_file) {
                            if (cnf == NULL) {
                                assert(sbs == NULL);
                                cnf = new BLib::CNF(orig_cnf_file, *anf,
                                              dat->extra_clauses, dat->config);
                                sbs = new BLib::SimplifyBySat(*cnf, dat->config);
                            } else {
                                no_cls = cnf->update();
                            }
                        } else {
                            delete cnf;
                            delete sbs;
                            cnf = new BLib::CNF(*anf, dat->config);
                            sbs = new BLib::SimplifyBySat(*cnf, dat->config);
                        }

                        lbool ret = sbs->simplify(dat->config.numConfl_lim,
                                      dat->config.numConfl_inc, dat->config.maxTime,
                                      no_cls, dat->learnt, *anf);

                        if (ret != l_False) {
                            for (size_t i = prevsz; i < dat->learnt.size(); ++i) {
                                num_learnt += anf->addLearntBoolePolynomial(dat->learnt[i]);
                            }
                        }
                    }
#endif
                    break;
            }

            if (dat->config.verbosity >= 2 && sub_iter_performed) {
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
                if (dat->config.verbosity >= 1) {
                    cout << "c [ANF Propagation] is false\n";
                }
                anf->setNOTOK();
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
        timeout = (cpuTime() > dat->config.maxTime);
    }

    if (dat->config.verbosity) {
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
    anf->contextualize(dat->learnt);
    return anf->getOK();
}

void Bosphorus::deduplicate()
{
    vector<BoolePolynomial> dedup;
    BLib::ANF::eqs_hash_t hash;
    for (const BoolePolynomial& p : dat->learnt) {
        if (hash.insert(p.hash()).second)
            dedup.push_back(p);
    }
    if (dat->config.verbosity >= 3) {
        cout << "c [Dedup] " << dat->learnt.size() << "->" << dedup.size() << endl;
    }
    dat->learnt.swap(dedup);
}

void Bosphorus::add_trivial_learnt_from_anf_to_learnt(ANF* a, ANF* o)
{
    auto anf = (BLib::ANF*)a;
    auto other = (BLib::ANF*)o;

    const BLib::ANF::eqs_hash_t& orig_eqs_hash = other->getEqsHash();

    // Add *NEW* assignments and equivalences
    for (uint32_t v = 0; v < anf->getRing().nVariables(); v++) {
        const lbool val = anf->value(v);
        const Lit lit = anf->getReplaced(v);
        BooleVariable bv = anf->getRing().variable(v);
        if (val != l_Undef) {
            BoolePolynomial assignment(bv + BooleConstant(val == l_True));
            if (orig_eqs_hash.find(assignment.hash()) == orig_eqs_hash.end())
                dat->learnt.push_back(assignment);
        } else if (lit != Lit(v, false)) {
            BooleVariable bv2 = anf->getRing().variable(lit.var());
            BoolePolynomial equivalence(bv + bv2 + BooleConstant(lit.sign()));
            if (orig_eqs_hash.find(equivalence.hash()) == orig_eqs_hash.end())
                dat->learnt.push_back(equivalence);
        }
    }
}

vector<Clause> Bosphorus::get_learnt(ANF* a)
{
    auto anf = (BLib::ANF*)a;

    vector<vector<int>> ret;
    auto anf2 = new BLib::ANF(&anf->getRing(), dat->config);
    for(const auto& l: dat->learnt) {
        anf2->addBoolePolynomial(l);
    }
    auto cnf = new BLib::CNF(*anf2, dat->config);
    return cnf->get_clauses_simple();
}

void Bosphorus::set_config(void* cfg)
{
    dat->config = *(BLib::ConfigData*)cfg;
}

const char* Bosphorus::get_compilation_env()
{
    return ::BLib::get_compilation_env();

}

const char* Bosphorus::get_version_tag()
{
    return ::BLib::get_version_tag();
}

const char* Bosphorus::get_version_sha1()
{
    return ::BLib::get_version_sha1();
}

bool Bosphorus::evaluate(ANF* a, const vector<lbool>& sol)
{
    auto anf = (BLib::ANF*)a;
    return anf->evaluate(sol);
}


void Bosphorus::print_stats(ANF* a)
{
    auto anf = (BLib::ANF*)a;
    anf->printStats();
}

ANF* Bosphorus::copy_anf_no_replacer(ANF* a)
{
    auto anf = (BLib::ANF*)a;
    auto orig_anf = new BLib::ANF(*anf, BLib::anf_no_replacer_tag());
    return (ANF*)orig_anf;
}

void Bosphorus::print_anf(ANF* a)
{
    auto anf = (BLib::ANF*)a;
    cout << *anf << endl;
}

size_t Bosphorus::get_learnt_size() const
{
    return dat->learnt.size();
}


void Bosphorus::delete_anf(ANF* a)
{
    auto anf = (BLib::ANF*)a;
    delete anf;
}
