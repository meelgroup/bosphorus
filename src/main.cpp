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

#include <sys/wait.h>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <iomanip>

#include "bosphorus.hpp"
#include "time_mem.h"
#include "configdata.hpp"

#include "bosphorus/solvertypesmini.hpp"
#include <boost/program_options.hpp>

using std::cerr;
using std::cout;
using std::deque;
using std::endl;
using std::string;

using namespace Bosph;
namespace po = boost::program_options;

//inputs and outputs
string anfInput;
string anfOutput;
string cnfInput;
string cnfOutput;

//solution map
string solmap_file_write;

// read/write
bool readANF;
bool readCNF;
bool writeANF;
bool writeCNF;

po::variables_map vm;
BLib::ConfigData config;


void print_solution(Solution& solution);
void check_solution(ANF* anf, Solution& solution);


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
    ("anfread", po::value(&anfInput), "Read ANF from this file")
    ("cnfread", po::value(&cnfInput), "Read CNF from this file")
    ("anfwrite", po::value(&anfOutput), "Write ANF output to file")
    ("cnfwrite", po::value(&cnfOutput), "Write CNF output to file")
    ("verb,v", po::value<uint32_t>(&config.verbosity)->default_value(config.verbosity),
     "Verbosity setting: 0(slient) - 3(noisy)")
    ("simplify", po::value<int>(&config.simplify)->default_value(config.simplify),
     "Simplify ANF")

    // Processes
    ("maxtime", po::value(&config.maxTime)->default_value(config.maxTime, maxTime_str.str()),
     "Stop solving after this much time (s); Use 0 if you only want to propagate")
    // checks
    ("comments", po::value(&config.writecomments)->default_value(config.writecomments),
     "Do not write comments to output files")
    ("solmap", po::value(&solmap_file_write), "Write solution map to this file")
    ;

    po::options_description cnf_conv_options("CNF conversion");
    cnf_conv_options.add_options()
    ("cutnum", po::value<uint32_t>(&config.cutNum)->default_value(config.cutNum),
     "Cutting number when not using XOR clauses")
    ("karn", po::value(&config.brickestein_algo_cutoff)->default_value(config.brickestein_algo_cutoff),
     "Uses this cutoff for doing Brickenstein's algorithm for translation of complex ANFs")
    ;

    po::options_description xl_options("XL");
    xl_options.add_options()
    ("xl", po::value(&config.doXL), "Turn on/off XL-based simplification. Default: ON")
    ("xldeg", po::value<uint32_t>(&config.xlDeg)->default_value(config.xlDeg),
     "Expansion degree for XL algorithm. Default = 1 (0 = Just GJE. For now we only support 0 <= xldeg = 3)")
    ("xlsample", po::value<double>(&config.XLsample)->default_value(config.XLsample),
     "Size of matrix to sample for XL, in log2")
    ("xlsamplex", po::value<double>(&config.XLsampleX)->default_value(config.XLsampleX),
     "Size of matrix to sample for XL, in log2, that we can expand by")
    ;

    po::options_description elimlin_options("ElimLin options");
    elimlin_options.add_options()
    ("el", po::value(&config.doEL), "Turn on/off ElimLin-based simplification. Default: ON")
    ("elsample", po::value<double>(&config.ELsample)->default_value(config.ELsample),
     "Size of matrixto sample for EL, in log2")
    ;

    po::options_description sat_options("SAT options");
    sat_options.add_options()
    ("sat", po::value(&config.doSAT),  "Turn on/off SAT-based simplification. Default: ON")
    ("satinc", po::value<uint64_t>(&config.numConfl_inc)->default_value(config.numConfl_inc),
     "Conflict inc for built-in SAT solver.")
    ("satlim", po::value<uint64_t>(&config.numConfl_lim)->default_value(config.numConfl_lim),
     "Conflict limit for built-in SAT solver.")
    ("threads,t", po::value<unsigned int>(&config.numThreads)->default_value(config.numThreads),
     "Number of threads to use for SAT solver (same value is used for built-in and external).")
    ;

    /* clang-format on */
    po::options_description cmdline_options;
    cmdline_options.add(generalOptions);
    cmdline_options.add(cnf_conv_options);
    cmdline_options.add(xl_options);
    cmdline_options.add(elimlin_options);
    cmdline_options.add(sat_options);

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
        cout << "bosphorus " << Bosphorus::get_version_sha1() << '\n'
             << Bosphorus::get_version_tag() << '\n'
             << Bosphorus::get_compilation_env() << endl;
        exit(0);
    }

    // I/O checks
    if (vm.count("anfread")) {
        readANF = true;
    }
    if (vm.count("cnfread")) {
        readCNF = true;
    }
    if (vm.count("anfwrite")) {
        writeANF = true;
    }
    if (vm.count("cnfwrite")) {
        writeCNF = true;
    }

    if (readANF && readCNF) {
        cout << "You cannot give both ANF/CNF files to read in\n";
        exit(-1);
    }

    // Config checks
    if (config.cutNum < 3 || config.cutNum > 10) {
        cout << "ERROR! For sanity, cutting number must be between 3 and 10\n";
        exit(-1);
    }
    if (config.brickestein_algo_cutoff > 20) {
        cout << "ERROR! For sanity, max Karnaugh table size is at most 20\n";
        exit(-1);
    }
    if (config.xlDeg > 3) {
        cout << "ERROR! We only currently support up to xldeg = 3\n";
        exit(-1);
    }

    if (config.verbosity) {
        cout << "c Bosphorus SHA revision " << Bosphorus::get_version_sha1() << endl;
        cout << "c Executed with command line: " << argv[0];
        for (int i = 1; i < argc; ++i)
            cout << ' ' << argv[i];
        cout << endl << "c Compilation env " << Bosphorus::get_compilation_env() << endl;
        cout << "c --- Configuration --\n"
             << "c maxTime = " << std::scientific << std::setprecision(2)
             << config.maxTime << std::fixed << endl
             << "c XL simp (deg = " << config.xlDeg
             << "; s = " << config.XLsample << '+' << config.XLsampleX
             << "): " << config.doXL << endl
             << "c EL simp (s = " << config.ELsample << "): " << config.doEL
             << endl
             << "c SAT simp (" << config.numConfl_inc << ':'
             << config.numConfl_lim << "): " << config.doSAT << endl
             << " using " << config.numThreads << " threads" << endl
             << "c Cut num: " << config.cutNum << endl
             << "c Brickenstein cutoff: " << config.brickestein_algo_cutoff << endl
             << "c --------------------" << endl;
    }
}

void write_solution_to_file(const char* fname, const Solution& solution)
{
    std::ofstream ofs;
    ofs.open(fname);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << fname << "\" for writing\n";
        exit(-1);
    }

    if (solution.ret == l_False) {
        ofs << "s UNSATISFIABLE" << endl;
    } else if (solution.ret == l_True) {
        ofs << "s SATISFIABLE" << endl;

        size_t num = 0;
        ofs << "v ";
        for (const lbool lit : solution.sol) {
            if (lit != l_Undef) {
                ofs << ((lit == l_True) ? "" : "-") << num << " ";
            }
            num++;
        }
        ofs << endl;
    } else {
        assert(false);
        exit(-1);
    }

    if (config.verbosity >= 2) {
        cout << "c [SAT] Solution written to " << fname << endl;
    }
    ofs.close();
}

int main(int argc, char* argv[])
{
    parseOptions(argc, argv);
    if (anfInput.length() == 0 && cnfInput.length() == 0) {
        cerr << "c ERROR: you must provide an ANF/CNF input file" << endl;
        exit(-1);
    }

    Bosphorus mylib;
    mylib.set_config((void*)&config);

    // Read from file
    ANF* anf = NULL;
    if (readANF) {
        double parseStartTime = cpuTime();
        anf = mylib.read_anf(anfInput.c_str());
        if (config.verbosity) {
            cout << "c [ANF Input] read in T: " << (cpuTime() - parseStartTime)
                 << endl;
        }
    }

    if (readCNF) {
        double parseStartTime = cpuTime();
        anf = mylib.read_cnf(cnfInput.c_str());
        if (config.verbosity) {
            cout << "c [CNF Input] read in T: " << (cpuTime() - parseStartTime)
                 << endl;
        }
    }
    assert(anf != NULL);
    if (config.verbosity >= 1) {
        Bosphorus::print_stats(anf);
    }

    // this is needed to check for test solution and the check if it is really a new learnt fact
    double myTime = cpuTime();
    if (config.verbosity) {
        cout << "c [ANF hash] Calculating ANF hash..." << endl;
    }

    auto orig_anf = Bosphorus::copy_anf_no_replacer(anf);
    if (config.verbosity) {
        cout << "c [ANF hash] Done. T: " << (cpuTime() - myTime) << endl;
    }

    if (config.simplify) {
        const char* cnf_orig = NULL;
        if (cnfInput.length() > 0) {
            cnf_orig = cnfInput.c_str();
        }
        cout << "c Simplifying...." << endl;
        mylib.simplify(anf, cnf_orig);
        cout << "c Simplifying finished." << endl;
    }
    if (config.printProcessedANF) {
        Bosphorus::print_anf(anf);
    }
    if (config.verbosity >= 1) {
        Bosphorus::print_stats(anf);
    }

    // finish up the learnt polynomials
    mylib.add_trivial_learnt_from_anf_to_learnt(anf, orig_anf);
    Bosphorus::delete_anf(orig_anf);

    // remove duplicates from learnt clauses
    mylib.deduplicate();

    // Write to file
    if (writeANF) {
        mylib.write_anf(anfOutput.c_str(), anf);
    }
    if (writeCNF) {
        const char* cnf_input = NULL;
        if (cnfInput.length() > 0) {
            cnf_input = cnfInput.c_str();
        }

        CNF* cnf = NULL;
        if (cnf_input != NULL) {
            cnf = mylib.write_cnf(cnf_input, cnfOutput.c_str(), anf);
        } else {
            cnf = mylib.write_cnf(cnfOutput.c_str(), anf);
        }
        if (!solmap_file_write.empty()) {
            std::ofstream ofs;
            ofs.open(solmap_file_write.c_str()); //std::ios_base::app
            if (!ofs) {
                std::cerr << "c Error opening file \"" << solmap_file_write
                          << "\" for writing solution map: solution -> simplified ANF\n";
                exit(-1);
            }

            mylib.write_solution_map(anf, &ofs);
            mylib.write_solution_map(cnf, &ofs);
        }
    }

    if (config.verbosity >= 1) {
        cout << "c Learnt " << mylib.get_learnt_size() << " fact(s) in " << cpuTime()
             << " seconds using "
             << static_cast<double>(memUsed()) / 1024.0 / 1024.0 << "MB.\n";
    }

    // clean up
    Bosphorus::delete_anf(anf);
    return 0;
}

/////////
// Unused functions
////////
// void write_xnf(const ANF* anf, const vector<BoolePolynomial>& learnt)
// {
//     XNF* xnf = new XNF(*anf, config);
//     std::ofstream ofs;
//     ofs.open(config.cnfOutput.c_str());
//     if (!ofs) {
//         std::cerr << "c Error opening file \"" << config.cnfOutput
//                   << "\" for writing\n";
//         exit(-1);
//     } else {
//         xnf->print_without_header(ofs);
//
//         ofs << "c Learnt " << learnt.size() << " fact(s)\n";
//         if (config.writecomments) {
//             for (const BoolePolynomial& poly : learnt) {
//                 ofs << "c " << poly << endl;
//             }
//         }
//     }
//     ofs.close();
// }

void print_solution(Solution& solution)
{
    assert(solution.ret != l_Undef);
    cout << "Solution ";
    cout << ((solution.ret == l_True) ? "SAT" : "UNSAT");

    size_t num = 0;
    std::stringstream toWrite;
    toWrite << "v ";
    for (const lbool lit : solution.sol) {
        if (lit != l_Undef) {
            toWrite << ((lit == l_True) ? "" : "-") << num << " ";
        }
        num++;
    }
    cout << toWrite.str() << endl;
}

void check_solution(ANF* anf, Solution& solution)
{
    //Checking
    bool goodSol = Bosphorus::evaluate(anf, solution.sol);
    if (!goodSol) {
        cout << "ERROR! Solution found is incorrect!" << endl;
        exit(-1);
    }
    if (config.verbosity >= 3) {
        cout << "c Solution found is correct." << endl;
    }
}
