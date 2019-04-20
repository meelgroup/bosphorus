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
#include "anf.hpp"
#include "cnf.hpp"
#include "dimacscache.hpp"
#include "gaussjordan.hpp"
#include "replacer.hpp"
#include "time_mem.h"
#include "library.h"

#ifdef SATSOLVE_ENABLED
#include "satsolve.hpp"
#endif
using std::cerr;
using std::cout;
using std::deque;
using std::endl;
using std::string;

ConfigData config;
po::variables_map vm;

void parseOptions(int argc, char* argv[])
{
    // Store executed arguments to print in output comments
    for (int i = 1; i < argc; i++) {
        config.executedArgs.append(string(argv[i]).append(" "));
    }

    std::ostringstream maxTime_str;
    maxTime_str << std::scientific << std::setprecision(2) << config.maxTime
                << std::fixed;

    /* clang-format off */
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
    ("verb,v", po::value<uint32_t>(&config.verbosity)->default_value(1),
     "Verbosity setting: 0(slient) - 3(noisy)")
    ("simplify", po::value<int>(&config.simplify)->default_value(config.simplify),
     "Simplify ANF")

    // Processes
    ("maxtime", po::value(&config.maxTime)->default_value(config.maxTime, maxTime_str.str()),
     "Stop solving after this much time (s); Use 0 if you only want to propagate")
    // checks
    ("paranoid", po::value<int>(&config.paranoid), "Run sanity checks")
    ("comments", po::value(&config.writecomments)->default_value(config.writecomments),
     "Do not write comments to output files")
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

#ifdef SATSOLVE_ENABLED
    po::options_description solving_processed_CNF_opts("CNF solving");
    solving_processed_CNF_opts.add_options()
    ("learnsolution", po::bool_switch(&config.learnSolution),
     "Make concrete the solution that SAT solver returns on the ANF representation. This is not strictly 'correct' as there may be other solutions(!) to the ANF")
    ("solvesat,s", po::bool_switch(&config.doSolveSAT), "Solve with SAT solver as per '--solverexe")
    ("solverexe,e", po::value(&config.solverExe)->default_value("/usr/local/bin/cryptominisat5"),
     "Solver executable for SAT solving CNF")
    ("solvewrite,o", po::value(&config.solutionOutput), "Write solver output to file")
    ;
#endif

    /* clang-format on */
    po::options_description cmdline_options;
    cmdline_options.add(generalOptions);
    cmdline_options.add(cnf_conv_options);
    cmdline_options.add(xl_options);
    cmdline_options.add(elimlin_options);
    cmdline_options.add(sat_options);
#ifdef SATSOLVE_ENABLED
    cmdline_options.add(solving_processed_CNF_opts);
#endif

    try {
        po::store(
            po::command_line_parser(argc, argv).options(cmdline_options).run(),
            vm);
        if (vm.count("help")) {
            cout << generalOptions << endl;
            cout << cnf_conv_options << endl;
            cout << xl_options << endl;
            cout << elimlin_options << endl;
            cout << sat_options << endl;
#ifdef SATSOLVE_ENABLED
            cout << solving_processed_CNF_opts << endl;
#endif
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

    if (config.verbosity) {
        cout << "c Bosphorus SHA revision " << get_version_sha1() << endl;
        cout << "c Executed with command line: " << argv[0];
        for (int i = 1; i < argc; ++i)
            cout << ' ' << argv[i];
        cout << endl << "c Compilation env " << get_compilation_env() << endl;
        cout << "c --- Configuration --\n"
             << "c maxTime = " << std::scientific << std::setprecision(2)
             << config.maxTime << std::fixed << endl
             << "c XL simp (deg = " << config.xlDeg
             << "; s = " << config.XLsample << '+' << config.XLsampleX
             << "): " << !config.noXL << endl
             << "c EL simp (s = " << config.ELsample << "): " << !config.noEL
             << endl
             << "c SAT simp (" << config.numConfl_inc << ':'
             << config.numConfl_lim << "): " << !config.noSAT << endl
             << "c Stop simplifying if SAT finds solution? "
             << (config.stopOnSolution ? "Yes" : "No") << endl
             << "c Paranoid: " << config.paranoid << endl
             << "c Cut num: " << config.cutNum << endl
             << "c Karnaugh size: " << config.maxKarnTableSize << endl
             << "c --------------------" << endl;
    }
}

void addTrivialFromANF(ANF* anf, vector<BoolePolynomial>& all_learnt,
                       const ANF::eqs_hash_t& orig_eqs_hash)
{
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

int main(int argc, char* argv[])
{
    parseOptions(argc, argv);
    if (config.anfInput.length() == 0 && config.cnfInput.length() == 0) {
        cerr << "c ERROR: you must provide an ANF/CNF input file" << endl;
    }

    Library mylib;
    mylib.set_config(config);

    // Read from file
    ANF* anf = nullptr;
    if (config.readANF) {
        double parseStartTime = cpuTime();
        anf = mylib.read_anf();
        if (config.verbosity) {
            cout << "c [ANF Input] read in T: " << (cpuTime() - parseStartTime)
            << endl;
        }
    }

    vector<Clause> cutting_clauses; ///<if we cut up the CNFs in the process, we must let the world know

    if (config.readCNF) {
        double parseStartTime = cpuTime();
        anf = mylib.read_cnf(cutting_clauses);
        if (config.verbosity) {
            cout << "c [CNF Input] read in T: " << (cpuTime() - parseStartTime)
            << endl;
        }
    }
    assert(anf != NULL);
    if (config.verbosity >= 1) {
        anf->printStats();
    }

    // this is needed to check for test solution and the check if it is really a new learnt fact
    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [ANF hash] Calculating ANF hash..." << endl;
    }
    ANF* orig_anf = nullptr;
    ANF::eqs_hash_t* orig_anf_hash = nullptr;

    if (config.paranoid)
        orig_anf = new ANF(*anf, anf_no_replacer_tag());
    else
        orig_anf_hash = new ANF::eqs_hash_t(anf->getEqsHash());
    if (config.verbosity) {
        cout << "c [ANF hash] Done. T: " << (cpuTime() - myTime) << endl;
    }

    // Perform simplifications
    vector<BoolePolynomial> learnt;

    if (config.simplify) {
        mylib.simplify(anf, learnt, orig_anf, cutting_clauses);
    }
    if (config.printProcessedANF) {
        cout << *anf << endl;
    }
    if (config.verbosity >= 1) {
        anf->printStats();
    }

#ifdef SATSOLVE_ENABLED
    // Solve processed CNF
    if (config.doSolveSAT) {
        mylib.solve_by_sat(anf, cutting_clauses, orig_anf);
    }
#endif

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
    mylib.deduplicate(learnt);

    // Write to file
    if (config.writeANF) {
        mylib.write_anf(anf);
    }
    if (config.writeCNF)
        mylib.write_cnf(anf, cutting_clauses, learnt);

    if (config.verbosity >= 1) {
        cout << "c Learnt " << learnt.size() << " fact(s) in " << cpuTime()
             << " seconds using "
             << static_cast<double>(memUsed()) / 1024.0 / 1024.0 << "MB.\n";
    }

    // clean up
    delete anf;
    return 0;
}
