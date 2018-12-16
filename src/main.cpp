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

#include <boost/program_options.hpp>
namespace po = boost::program_options;

#include <sys/wait.h>
#include <deque>
#include <fstream>
#include <memory>

#include "GitSHA1.h"
#include "anf.h"
#include "cnf.h"
#include "dimacscache.h"
#include "gaussjordan.h"
#include "replacer.h"
#include "satsolve.h"
#include "simplifybysat.h"
#include "time_mem.h"

using std::cerr;
using std::cout;
using std::deque;
using std::endl;
using std::string;

ConfigData config;
BoolePolyRing* polybori_ring = nullptr;

void parseOptions(int argc, char* argv[]) {
    // Store executed arguments to print in output comments
    for (int i = 1; i < argc; i++) {
        config.executedArgs.append(string(argv[i]).append(" "));
    }

    std::ostringstream maxTime_str;
    maxTime_str << std::sclientific << std::setprecision(2) << config.maxTime << std::fixed;

    // Declare the supported options.
    po::options_description generalOptions("Main options");
    generalOptions.add_options()
    ("help,h", "produce help message")
    ("version", "print version number and exit")
    // Input/Output
    ("anfread", po::value(&config.anfInput), "Read ANF from this file")
    ("cnfread", po::value(&config.cnfInput), "Read CNF from this file")
    ("anfwrite", po::value(&config.anfOutput), "Write ANF output to file")
    ("cnfwrite", po::value(&config.cnfOutput), "Write CNF output to file")
    ("writecomments", po::bool_switch(&config.writecomments), "Do not write comments to output files")
    ("verbosity,v", po::value<uint32_t>(&config.verbosity)->default_value(1),
     "Verbosity setting (0 = silent); only levels 0 to 3 have been tested for sanity. Any higher may give you too much information.")

    // Processes
    ("maxtime", po::value(&config.maxTime)->default_value(config.maxTime, maxTime_str.str()),
     "Stop solving after this much time (s); Use 0 if you do not want to do fact-finding")
    // checks
    ("notparanoid", po::bool_switch(&config.notparanoid), "no sanity checks")
    ;

    po::options_description cnf_conv_options("CNF conversion");
    cnf_conv_options.add_options()
    ("cutnum", po::value<uint32_t>(&config.cutNum)->default_value(config.cutNum),
     "Cutting number when not using XOR clauses")
    ("karn", po::value(&config.maxKarnTableSize)->default_value(8),
     "Sets Karnaugh map dimension during CNF conversion")
    ;

    po::options_description xl_options("XL");
    xl_options.add_options()
    ("noxl", po::bool_switch(&config.noXL), "No XL")
    ("xldeg", po::value<uint32_t>(&config.xlDeg)->default_value(1),
     "Expansion degree for XL algorithm. Default = 1 (0 = Just GJE. For now we only support 0 <= xldeg = 3)")
    ("xlsample", po::value<double>(&config.XLsample)->default_value(config.XLsample),
     "Size of matrix to sample for XL, in log2")
    ("xlsamplex", po::value<double>(&config.XLsampleX)->default_value(config.XLsampleX),
     "Size of matrix to sample for XL, in log2, that we can expand by")
    ;

    po::options_description elimlin_options("ElimLin options");
    elimlin_options.add_options()
    ("noel", po::bool_switch(&config.noEL), "no ElimLin")
    ("elsample", po::value<double>(&config.ELsample)->default_value(config.ELsample),
     "Size of matrixto sample for EL, in log2")
    ;

    po::options_description sat_options("SAT options");
    sat_options.add_options()
    ("nosat", po::bool_switch(&config.noSAT),  "No SAT solving")
    ("stoponsolution", po::bool_switch(&config.stopOnSolution),
     "Stops further simplifications and store solution if SAT simp finds a solution")
    ("satinc", po::value<uint64_t>(&config.numConfl_inc)->default_value(config.numConfl_inc),
     "Conflict inc for built-in SAT solver.")
    ("satlim", po::value<uint64_t>(&config.numConfl_lim)->default_value(config.numConfl_lim),
     "Conflict limit for built-in SAT solver.")
    ;

    po::options_description solving_processed_CNF_opts("CNF solving");
    solving_processed_CNF_opts.add_options()
    ("learnsolution", po::bool_switch(&config.learnSolution),
     "Make concrete the solution that SAT solver returns on the ANF representation. This is not strictly 'correct' as there may be other solutions(!) to the ANF")
    ("solvesat,s", po::bool_switch(&config.doSolveSAT), "Solve with SAT solver as per '--solverexe")
    ("solverexe,e", po::value(&config.solverExe)->default_value("/usr/local/bin/cryptominisat5"),
     "Solver executable for SAT solving CNF")
    ("solvewrite,o", po::value(&config.solutionOutput), "Write solver output to file")
    ;

    po::variables_map vm;
    po::options_description cmdline_options;
    cmdline_options.add(generalOptions);
    cmdline_options.add(cnf_conv_options);
    cmdline_options.add(xl_options);
    cmdline_options.add(elimlin_options);
    cmdline_options.add(sat_options);
    cmdline_options.add(solving_processed_CNF_opts);

    try {
        po::store(
            po::command_line_parser(argc, argv).options(cmdline_options).run(),
            vm);
        if (vm.count("help")) {
            cout << generalOptions << endl;
            cout <<  cnf_conv_options << endl;
            cout <<  xl_options << endl;
            cout <<  elimlin_options << endl;
            cout <<  sat_options << endl;
            cout <<  solving_processed_CNF_opts << endl;
            exit(0);
        }
        po::notify(vm);
    } catch (boost::exception_detail::clone_impl<
             boost::exception_detail::error_info_injector<po::unknown_option> >&
                 c) {
        cout << "Some option you gave was wrong. Please give '--help' to get "
                "help"
             << endl;
        cout << "Unkown option: " << c.what() << endl;
        exit(-1);
    } catch (boost::bad_any_cast& e) {
        cerr << e.what() << endl;
        exit(-1);
    } catch (boost::exception_detail::clone_impl<
             boost::exception_detail::error_info_injector<
                 po::invalid_option_value> >& what) {
        cerr << "Invalid value '" << what.what() << "'"
             << " given to option '" << what.get_option_name() << "'" << endl;
        exit(-1);
    } catch (boost::exception_detail::clone_impl<
             boost::exception_detail::error_info_injector<
                 po::multiple_occurrences> >& what) {
        cerr << "Error: " << what.what() << " of option '"
             << what.get_option_name() << "'" << endl;
        exit(-1);
    } catch (
        boost::exception_detail::clone_impl<
            boost::exception_detail::error_info_injector<po::required_option> >&
            what) {
        cerr << "You forgot to give a required option '"
             << what.get_option_name() << "'" << endl;
        exit(-1);
    }

    if (vm.count("version")) {
        cout << "bosphorus " << get_version_sha1() << '\n'
             << get_version_tag() << '\n'
             << get_compilation_env() << endl;
        exit(0);
    }

    // I/O checks
    if (vm.count("anfread")) {
        config.readANF = true;
    }
    if (vm.count("cnfread")) {
        config.readCNF = true;
    }
    if (vm.count("anfwrite")) {
        config.writeANF = true;
    }
    if (vm.count("cnfwrite")) {
        config.writeCNF = true;
    }
    if (!config.readANF && !config.readCNF) {
        cout << "You must give an ANF/CNF file to read in\n";
        exit(-1);
    }
    if (config.readANF && config.readCNF) {
        cout << "You cannot give both ANF/CNF files to read in\n";
        exit(-1);
    }

    // Config checks
    if (config.cutNum < 3 || config.cutNum > 10) {
        cout << "ERROR! For sanity, cutting number must be between 3 and 10\n";
        exit(-1);
    }
    if (config.maxKarnTableSize > 20) {
        cout << "ERROR! For sanity, max Karnaugh table size is at most 20\n";
        exit(-1);
    }
    if (config.xlDeg > 3) {
        cout << "ERROR! We only currently support up to xldeg = 3\n";
        exit(-1);
    }
    if (config.doSolveSAT && !vm.count("solvewrite")) {
        cout << "ERROR! Provide a output file for the solving of the solved "
                "CNF with '--solvewrite x'\n";
        exit(-1);
    }

    if (config.verbosity >= 1) {
        cout << "c " << argv[0];
        for (int i = 1; i < argc; ++i)
            cout << ' ' << argv[i];
        cout << "\nc Compilation env " << get_compilation_env() << endl;
        cout << "c --- Configuration --\n"
             << "c maxTime = " << config.maxTime << '\n'
             << "c XL simp (deg = " << config.xlDeg
             << "; s = " << config.XLsample << '+' << config.XLsampleX
             << "): " << !config.noXL << endl
             << "c EL simp (s = " << config.ELsample << "): " << !config.noEL
             << endl
             << "c SAT simp (" << config.numConfl_inc << ':'
             << config.numConfl_lim << "): " << !config.noSAT << endl
             << "c Stop simplifying if SAT finds solution? "
             << (config.stopOnSolution ? "Yes" : "No") << endl
             << "c Paranoid: " << (config.notparanoid ? "No" : "Yes") << endl
             << "c Cut num: " << config.cutNum << endl
             << "c Karnaugh size: " << config.maxKarnTableSize << endl
             << "c --------------------\n";
    }
}

ANF* read_anf() {
    // Find out maxVar in input ANF file
    size_t maxVar = ANF::readFileForMaxVar(config.anfInput);

    // Construct ANF
    // ring size = maxVar + 1, because ANF variables start from x0
    polybori_ring = new BoolePolyRing(maxVar + 1);
    ANF* anf = new ANF(polybori_ring, config);
    anf->readFile(config.anfInput);
    return anf;
}

ANF* read_cnf(vector<Clause>& extra_clauses) {
    DIMACSCache dimacs_cache(config.cnfInput.c_str());
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

CNF* anf_to_cnf(const ANF* anf, const vector<Clause>& cutting_clauses) {
    double convStartTime = cpuTime();
    CNF* cnf = new CNF(*anf, cutting_clauses, config);
    if (config.verbosity >= 2) {
        cout << "c [CNF conversion] in " << (cpuTime() - convStartTime)
             << " seconds.\n";
        cnf->printStats();
    }
    return cnf;
}

void write_anf(const ANF* anf) {
    std::ofstream ofs;
    ofs.open(config.anfOutput.c_str());
    if (!ofs) {
        std::cerr << "c Error opening file \"" << config.anfOutput
                  << "\" for writing\n";
        exit(-1);
    } else {
        ofs << "c Executed arguments: " << config.executedArgs << endl;
        ofs << *anf << endl;
    }
    ofs.close();
}

void write_cnf(const ANF* anf, const vector<Clause>& cutting_clauses,
               const vector<BoolePolynomial>& learnt) {
    CNF* cnf = anf_to_cnf(anf, cutting_clauses);
    std::ofstream ofs;
    ofs.open(config.cnfOutput.c_str());
    if (!ofs) {
        std::cerr << "c Error opening file \"" << config.cnfOutput
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

void deduplicate(vector<BoolePolynomial>& learnt) {
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

void simplify(ANF* anf, vector<BoolePolynomial>& loop_learnt,
              const ANF* orig_anf, const vector<Clause>& cutting_clauses) {
    bool timeout = (cpuTime() > config.maxTime);
    if (timeout) {
        if (config.verbosity) {
            cout << "c Timeout before learning" << endl;
        }
        return;
    }

    double loopStartTime = cpuTime();
    // Perform initial propagation to avoid needing >= 2 iterations
    anf->propagate();
    timeout = (cpuTime() > config.maxTime);

    bool foundSolution = false;
    bool changes[] = {true, true, true}; // any changes for the strategies
    size_t waits[] = {0, 0, 0};
    size_t countdowns[] = {0, 0, 0};
    uint32_t numIters = 0;
    unsigned char subIters = 0;
    CNF* cnf = nullptr;
    SimplifyBySat* sbs = nullptr;

    while (
        !timeout && anf->getOK() &&
        (!config.stopOnSolution || !foundSolution) &&
        std::accumulate(changes, changes + 3, false, std::logical_or<bool>())) {
        static const char* strategy_str[] = {"XL", "ElimLin", "SAT"};
        const double startTime = cpuTime();
        int num_learnt = 0;

        if (countdowns[subIters] > 0) {
            if (config.verbosity >= 2) {
                cout << "c [" << strategy_str[subIters] << "] waiting for "
                     << countdowns[subIters] << " iteration(s)." << endl;
            }
        } else {
            switch (subIters) {
                case 0:
                    if (!config.noXL)
                        num_learnt = anf->extendedLinearization(loop_learnt);
                    break;
                case 1:
                    if (!config.noEL)
                        num_learnt = anf->elimLin(loop_learnt);
                    break;
                case 2:
                    if (!config.noSAT) {
                        size_t beg = 0;
                        if (cnf == nullptr)
                            cnf = new CNF(*anf, cutting_clauses, config);
                        else
                            beg = cnf->update();
                        if (sbs == nullptr)
                            sbs = new SimplifyBySat(*cnf, config);
                        num_learnt = sbs->simplify(
                            config.numConfl_lim, config.numConfl_inc,
                            config.maxTime, beg, loop_learnt, foundSolution,
                            *anf, orig_anf);
                    }
                    break;
            }

            if (config.verbosity >= 2) {
                cout << "c [" << strategy_str[subIters] << "] learnt "
                     << num_learnt << " new facts in "
                     << (cpuTime() - startTime) << " seconds." << endl;
            }
        }

        changes[subIters] = (num_learnt > 0);
        if (changes[subIters]) {
            waits[subIters] = 0;
            bool ok = anf->propagate();
            if (!ok) {
                if (config.verbosity >= 1)
                    cout << "c [ANF Propagation] is false\n";
            }
        } else {
            if (countdowns[subIters] > 0)
                --countdowns[subIters];
            else {
                static size_t series[] = {0, 1,  1,  2,  3,  5,
                                          8, 13, 21, 34, 55, 89};
                countdowns[subIters] = series[std::min(
                    waits[subIters], sizeof(series) / sizeof(series[0]) - 1)];
                ++waits[subIters];
            }
        }

        if (subIters < 2)
            ++subIters;
        else {
            ++numIters;
            subIters = 0;
            deduplicate(loop_learnt); // because this is a good time to do this
        }
        timeout = (cpuTime() > config.maxTime);
    }

    if (config.verbosity >= 2) {
        cout << "c ["
             << (timeout
                     ? "Timeout"
                     : ((config.stopOnSolution && foundSolution)
                            ? "Solution"
                            : (!anf->getOK() ? "No-solution" : "Fixed-Point")))
             << " after " << numIters << '.' << static_cast<size_t>(subIters)
             << " iteration(s) in " << (cpuTime() - loopStartTime)
             << " seconds.]\n";
    }

    if (sbs != nullptr)
        delete sbs;
    if (cnf != nullptr)
        delete cnf;
    anf->contextualize(loop_learnt);
}

void addTrivialFromANF(ANF* anf, vector<BoolePolynomial>& all_learnt,
                       const ANF::eqs_hash_t& orig_eqs_hash) {
    // Add *NEW* assignments and equivalences
    for (uint32_t v = 0; v < anf->getRing().nVariables(); v++) {
        const lbool val = anf->value(v);
        const Lit lit = anf->getReplaced(v);
        BooleVariable bv = anf->getRing().variable(v);
        if (val != l_Undef) {
            BoolePolynomial assignment(bv + BooleConstant(val == l_True));
            if (orig_eqs_hash.find(assignment.hash()) == orig_eqs_hash.end())
                all_learnt.push_back(assignment);
        } else if (lit != Lit(v, false)) {
            BooleVariable bv2 = anf->getRing().variable(lit.var());
            BoolePolynomial equivalence(bv + bv2 + BooleConstant(lit.sign()));
            if (orig_eqs_hash.find(equivalence.hash()) == orig_eqs_hash.end())
                all_learnt.push_back(equivalence);
        }
    }
}

void solve_by_sat(const ANF* anf, const vector<Clause>& cutting_clauses,
                  const ANF* orig_anf) {
    CNF* cnf = anf_to_cnf(anf, cutting_clauses);
    SATSolve solver(config.verbosity, !config.notparanoid, config.solverExe);
    vector<lbool> sol = solver.solveCNF(orig_anf, *anf, *cnf);
    std::ofstream ofs;
    ofs.open(config.solutionOutput.c_str());
    if (!ofs) {
        std::cerr << "c Error opening file \"" << config.solutionOutput
                  << "\" for writing\n";
        exit(-1);
    } else {
        size_t num = 0;
        ofs << "v ";
        for (const lbool lit : sol) {
            if (lit != l_Undef) {
                ofs << ((lit == l_True) ? "" : "-") << num << " ";
            }
            num++;
        }
        ofs << endl;
    }
    ofs.close();
}

int main(int argc, char* argv[]) {
    parseOptions(argc, argv);
    if (config.anfInput.length() == 0 && config.cnfInput.length() == 0) {
        cerr << "c ERROR: you must provide an ANF/CNF input file" << endl;
    }

    // Read from file
    ANF* anf = nullptr;
    if (config.readANF) {
        double parseStartTime = cpuTime();
        anf = read_anf();
        if (config.verbosity >= 2) {
            cout << "c [ANF Input] in " << (cpuTime() - parseStartTime)
                 << " seconds.\n";
        }
    }

    vector<Clause>
        cutting_clauses; // in case we cut up the CNFs in the process, we must let the world know
    if (config.readCNF) {
        double parseStartTime = cpuTime();
        anf = read_cnf(cutting_clauses);
        if (config.verbosity >= 2) {
            cout << "c [CNF Input] in " << (cpuTime() - parseStartTime)
                 << " seconds.\n";
        }
    }
    assert(anf != NULL);
    if (config.verbosity >= 1) {
        anf->printStats();
    }

    // this is needed to check for test solution and the check if it is really a new learnt fact
    ANF* orig_anf = nullptr;
    ANF::eqs_hash_t* orig_anf_hash = nullptr;

    if (!config.notparanoid)
        orig_anf = new ANF(*anf, anf_no_replacer_tag());
    else
        orig_anf_hash = new ANF::eqs_hash_t(anf->getEqsHash());

    // Perform simplifications
    vector<BoolePolynomial> learnt;
    simplify(anf, learnt, orig_anf, cutting_clauses);
    if (config.printProcessedANF) {
        cout << *anf << endl;
    }
    if (config.verbosity >= 1) {
        anf->printStats();
    }

    // Solve processed CNF
    if (config.doSolveSAT) {
        solve_by_sat(anf, cutting_clauses, orig_anf);
    }

    // finish up the learnt polynomials
    if (orig_anf_hash != nullptr)
        addTrivialFromANF(anf, learnt, *orig_anf_hash);
    else
        addTrivialFromANF(anf, learnt, orig_anf->getEqsHash());

    // Free some space in case wee need the memory later
    if (orig_anf != nullptr)
        delete orig_anf;
    if (orig_anf_hash != nullptr)
        delete orig_anf_hash;

    // remove duplicates from learnt clauses
    deduplicate(learnt);

    // Write to file
    if (config.writeANF) {
        write_anf(anf);
    }
    if (config.writeCNF)
        write_cnf(anf, cutting_clauses, learnt);

    if (config.verbosity >= 1) {
        cout << "c Learnt " << learnt.size() << " fact(s) in " << cpuTime()
             << " seconds using "
             << static_cast<double>(memUsed()) / 1024.0 / 1024.0 << "MB.\n";
    }

    // clean up
    delete anf;
    delete polybori_ring;
    return 0;
}
