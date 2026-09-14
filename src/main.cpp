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

#include <charconv>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <stdexcept>
#include <string>

#include "bosphorus.hpp"
#include "time_mem.h"
#include "configdata.hpp"

#include <cryptominisat5/solvertypesmini.h>
#include <cryptominisat5/cryptominisat.h>
#include "argparse.hpp"

using std::cerr;
using std::cout;
using std::endl;
using std::string;

using namespace Bosph;

//inputs and outputs
string anfInput;
string anfOutput;
string cnfInput;
string cnfOutput;
string solution_output_file;

//solution map
string solmap_file_write;

// read/write
bool readANF;
bool readCNF;
bool writeANF;
bool writeCNF;
bool solve_with_cms;
bool all_solutions;
int only_new_cnf_clauses = 0;
uint32_t maxiters = 100;
uint32_t max_sol = 1;

argparse::ArgumentParser program("bosphorus", "", argparse::default_arguments::help);
BLib::ConfigData config;

void solve(Bosph::Bosphorus* mylib, CNF* cnf, ANF* anf);

// Converters for argparse: strict, whole-string parsing so that "3x" or
// "1.5" given to an integer option is rejected instead of silently truncated.
template<typename T>
static T fc_integral(const std::string& s)
{
    T val = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec != std::errc{}) {
        throw std::invalid_argument("not an integer in range: '" + s + "'");
    }
    if (ptr != s.data() + s.size()) {
        throw std::invalid_argument("trailing characters in integer: '" + s + "'");
    }
    return val;
}
static double fc_double(const std::string& s)
{
    size_t pos = 0;
    double val;
    try {
        val = std::stod(s, &pos);
    } catch (const std::exception&) {
        throw std::invalid_argument("not a number: '" + s + "'");
    }
    if (pos != s.size()) {
        throw std::invalid_argument("trailing characters in number: '" + s + "'");
    }
    return val;
}
static bool fc_bool(const std::string& s)
{
    std::string l = s;
    for (auto& c : l) c = std::tolower(c);
    if (l == "1" || l == "true" || l == "yes" || l == "on") return true;
    if (l == "0" || l == "false" || l == "no" || l == "off") return false;
    throw std::invalid_argument("not a boolean (0/1/true/false): '" + s + "'");
}

template<typename T, typename F>
static void add_arg(const char* name, T& var, F fun, const char* hhelp)
{
    program.add_argument(name)
        .action([&var, fun](const auto& a) { var = fun(a); })
        .default_value(var)
        .help(hhelp);
}
template<typename T, typename F>
static void add_arg2(const char* name1, const char* name2, T& var, F fun, const char* hhelp)
{
    program.add_argument(name1, name2)
        .action([&var, fun](const auto& a) { var = fun(a); })
        .default_value(var)
        .help(hhelp);
}
// Option that takes a value but has no default, e.g. a file name.
static void add_str_arg(const char* name, string& var, const char* hhelp)
{
    program.add_argument(name)
        .action([&var](const auto& a) { var = a; })
        .help(hhelp);
}
static void add_flag(const char* name, bool& var, const char* hhelp)
{
    program.add_argument(name)
        .action([&var](const auto&) { var = true; })
        .flag()
        .help(hhelp);
}

void parseOptions(int argc, char* argv[])
{
    // Store executed arguments to print in output comments
    for (int i = 1; i < argc; i++) {
        config.executedArgs.append(string(argv[i]).append(" "));
    }

    program.add_description("ANF and CNF simplifier and converter");

    /* clang-format off */
    // Main options
    program.add_argument("--version")
        .action([&](const auto&) {
            cout << "bosphorus " << Bosphorus::get_version_sha1() << '\n'
                 << Bosphorus::get_version_tag() << '\n'
                 << Bosphorus::get_compilation_env() << endl;
            exit(0);
        })
        .flag()
        .help("print version number and exit");
    // Input/Output
    program.add_argument("input")
        .nargs(argparse::nargs_pattern::optional)
        .default_value(string())
        .help("Input file. Treated as --anfread if it ends in .anf, --cnfread if it ends in .cnf");
    add_str_arg("--anfread", anfInput, "Read ANF from this file");
    add_str_arg("--cnfread", cnfInput, "Read CNF from this file");
    add_str_arg("--anfwrite", anfOutput, "Write ANF output to file");
    add_str_arg("--cnfwrite", cnfOutput, "Write CNF output to file");
    add_arg2("-v", "--verb", config.verbosity, fc_integral<uint32_t>,
        "Verbosity setting: 0(slient) - 3(noisy)");
    add_arg("--simplify", config.simplify, fc_integral<int>, "Simplify ANF");
    add_arg("--color", config.color, fc_integral<int>,
        "Colour the [simp-stats] lines: 0 = never, 1 = always, 2 = auto (terminal and NO_COLOR unset)");
    add_flag("--solve", solve_with_cms, "Solve the resulting ANF (built-in CryptoMiniSat with Gauss-Jordan and XOR recovery on)");
    add_str_arg("--solvewrite", solution_output_file,
        "Solve the resulting ANF and print the solution to this file");
    add_flag("--allsol", all_solutions, "Enumerate all solutions with the built-in solver, one SAT call per solution: fine up to some 10000 solutions, use ApproxMC on the written CNF beyond that");
    add_arg("--maxsol", max_sol, fc_integral<uint32_t>, "Find at most this many solutions");
    add_arg("--maxiters", maxiters, fc_integral<uint32_t>, "Maximum iterations to simplify");

    // Processes
    add_arg("--maxtime", config.maxTime, fc_double,
        "Stop solving after this much time (s); Use 0 if you only want to propagate");
    // checks
    add_arg("--comments", config.writecomments, fc_bool,
        "Do not write comments to output files");
    add_arg("--projshow", config.projShow, fc_integral<int>,
        "Write a 'c p show' line into the CNF: 0 never, 1 always (all original variables), "
        "2 when the input carried a projection set ('c p show ... END' in the ANF), listing the "
        "CNF variables of exactly those ANF variables, so that solution counts over the projection "
        "agree between ANF and CNF. Default: 2");

    // CNF conversion
    add_arg("--cutnum", config.cutNum, fc_integral<uint32_t>,
        "Cutting number when not using XOR clauses");
    add_arg("--factor", config.doFactor, fc_integral<int>,
        "Encode a polynomial that is a product of linear factors, (l1+c1)*(l2+c2)*..., as one clause over one shared CNF variable per linear factor instead of one variable per monomial. Default: ON");
    add_arg("--xorcls", config.xorClauses, fc_integral<int>,
        "Write XORs as native CryptoMiniSat xor clauses ('x 1 2 3 0' lines) instead of cutting them into CNF. The output is then CNF-XOR, which only CryptoMiniSat reads; --solve always uses them. Default: OFF");
    add_arg("--xormaxlen", config.xorMaxLen, fc_integral<uint32_t>,
        "With --xorcls 1: cut native xor clauses longer than this into a chain of native pieces of this length (0 = never cut). Default: 0");
    add_arg("--partner", config.doPartner, fc_integral<int>,
        "ANF-to-CNF partner strategies (Jovanovic & Kreuzer): fold x*y+x, x*y+x+y+1, x*y+x*z, x*y*z+x*y*w and their generalisations into one CNF variable each. Default: ON");
    add_arg("--karn", config.brickestein_algo_cutoff, fc_integral<uint32_t>,
        "Uses this cutoff for doing Brickenstein's algorithm for translation of complex ANFs");
    add_arg("--karncluster", config.karnCluster, fc_integral<uint32_t>,
        "Encode small nonlinear equations that share variables jointly (one clause set over the union of their variables, e.g. all equations of an S-box) when the union has at most this many variables. 0 = off. Default: 10");
    add_arg("--onlynewcnfcls", only_new_cnf_clauses, fc_integral<int>,
        "Only output to CNF the newly discovered CNF clauses. Must have CNF as input.");

    // In-place ANF rewrite rules
    add_arg("--rewrite", config.doRewrite, fc_integral<int>,
        "Turn on/off all in-place ANF rewrite rules. Default: ON");
    add_arg("--lingauss", config.doLinGauss, fc_integral<int>,
        "Rewrite rule lin-gauss: Gaussian elimination among the linear equations, shortest first; deletes the redundant ones, shortens the others and finds units and equivalences. Default: ON");
    add_arg("--spanfilter", config.spanFilter, fc_integral<int>,
        "Drop facts learnt by XL/ElimLin/SAT that are linear combinations of the linear equations already in the system (they add nothing but XORs to the CNF). Default: ON");
    add_arg("--binomred", config.doBinomRed, fc_integral<int>,
        "Rewrite rule binom-red: reduce all equations modulo monomial and binomial equations (x*y+x=0 turns x*y*z into x*z). Default: ON");
    add_arg("--binomredlen", config.binomRedLen, fc_integral<uint32_t>,
        "Max number of terms of an equation used as a rule by binom-red. Default: 2");
    add_arg("--shorten", config.doShorten, fc_integral<int>,
        "Rewrite rule poly-shorten: replace p by p+f whenever that has fewer terms. Default: ON");
    add_arg("--monogauss", config.doMonoGauss, fc_integral<int>,
        "Rewrite rule mono-gauss: Gaussian elimination among all equations with one column per monomial (linearisation), shortest first; deletes the equations that are combinations of others, replaces an equation by its reduced form when that is shorter or of lower degree (a linear combination of nonlinear equations that cancels every nonlinear monomial is a new linear equation). Default: ON");
    add_arg("--monogausslen", config.monoGaussLen, fc_integral<size_t>,
        "mono-gauss: only equations with at most this many terms take part. Default: 64");
    add_arg("--monogausscols", config.monoGaussCols, fc_integral<size_t>,
        "mono-gauss: not run when the participating equations have more distinct monomials than this. Default: 100000");
    add_arg("--monogaussshorten", config.monoGaussShorten, fc_integral<int>,
        "mono-gauss: replace an equation by a shorter combination of the same degree: 0 = never (only delete redundant equations and keep combinations of lower degree, i.e. linear consequences), 1 = linear equations only (a second XOR shortening with the monomial order as pivot order: 11% smaller CNFs and faster CryptoMiniSat on the bivium family, but 2x slower CryptoMiniSat on two of five ascon instances), 2 = all degrees (same effect on ascon). Default: 0");
    add_arg("--prodsplit", config.doProdSplit, fc_integral<int>,
        "Rewrite rule prod-split: an equation p = 0 where 1 + p is a product of linear factors (l1+c1)*(l2+c2)*... becomes the linear equations l1+c1+1 = 0, l2+c2+1 = 0, ... (every factor must be 1); generalises 'x*y*z + 1 = 0 sets x, y, z'. Default: ON");
    add_arg("--probe", config.doProbe, fc_integral<int>,
        "Rewrite rule lit-probe: forced literals, equivalences and implication-graph SCCs from small equations. Default: ON");
    add_arg("--cnfprobe", config.doCnfProbe, fc_integral<int>,
        "Rewrite rule cnf-probe: the system is converted to CNF and CryptoMiniSat's inprocessing runs on it as Arjun does (equivalent-literal SCCs, probing of every ANF variable, in-tree probing, no variable elimination); the literals fixed at level 0 and the equivalent literals come back as equations (a CNF variable stands for a monomial, an XOR cut or a lineral). Runs when the other rules have reached a fixed point. Default: ON");
    add_arg("--cnfprobevars", config.cnfProbeVars, fc_integral<size_t>,
        "cnf-probe: probe at most this many ANF variables, the most incident first. Default: 200000");
    add_arg("--varprobe", config.doVarProbe, fc_integral<int>,
        "Rewrite rule var-probe: failed-literal probing with propagation, as CNF preprocessors do it: x = 0 and x = 1 are each propagated through the equations (units, m+1, products with one factor left); a branch that runs into 1 = 0 forces x, a variable set the same way in both branches is set, one set opposite ways is equivalent to x. Only units propagate, so it finds nothing on the S-box and stream-cipher families. Default: OFF");
    add_arg("--varprobebudget", config.varProbeBudget, fc_integral<uint64_t>,
        "var-probe: equation evaluations per call, a deterministic work budget. Default: 2e6");
    add_arg("--varprobelen", config.varProbeLen, fc_integral<size_t>,
        "var-probe: polynomial equations with more terms than this are not evaluated (products of linear factors always are). Default: 64");
    add_arg("--probevars", config.probeVars, fc_integral<uint32_t>,
        "lit-probe only looks at equations with at most this many variables. Default: 8");
    add_arg("--faccanon", config.doFacCanon, fc_integral<int>,
        "Rewrite rule fac-canon: reduce every linear factor of a product modulo the linear equations and use the shortest representative of its class, so equal factors become identical and short. Makes the CNF smaller but CryptoMiniSat slower on average on the bivium family. Default: OFF");
    add_arg("--facres", config.doFacRes, fc_integral<int>,
        "Rewrite rule fac-res: resolution between products sharing a linear factor with opposite constants; a resolvent with one factor is a new linear equation. Default: OFF");
    add_arg("--facresmax", config.facResMaxFactors, fc_integral<uint32_t>,
        "fac-res only adds resolvents with at most this many factors. Default: 2");
    add_arg("--gb", config.doGB, fc_integral<int>,
        "Rewrite rule gb-cone (Groebner bases): 0 = off, 1 = on for every system (cones of small equations sharing variables, short basis members are added), 2 = only the complete basis of a system with at most --gbwholevars variables. Default: 2");
    add_arg("--gbfull", config.gbFull, fc_integral<int>, "gb-cone: 0 = degree-bounded Buchberger loop (--gbdeg) in the lexicographic main ring per cone; 1 = complete Groebner basis of every cone with BRiAl's symmGB_F2 in a degree-ordered ring; 2 = both per cone (the orderings find different consequences) and the complete basis for a whole small system (--gbwholevars). Default: 2");
    add_arg("--gbdeg", config.gbDeg, fc_integral<uint32_t>, "gb-cone: with --gbfull 0, drop S-polynomials above this degree. Default: 3");
    add_arg("--gbwindow", config.gbWindow, fc_integral<uint32_t>, "gb-cone: at most this many equations per cone. Default: 24");
    add_arg("--gbmaxvars", config.gbMaxVars, fc_integral<uint32_t>, "gb-cone: a cone grows while its equations use at most this many variables. Default: 16");
    add_arg("--gbengine", config.gbEngine, fc_integral<int>, "gb-cone: engine for the complete bases: 0 = BRiAl's symmGB_F2, 1 = Bosphorus's matrix F4 over the Boolean ring (M4RI, up to 64 variables per cone), 2 = matrix F5 (signature criterion, no reductions to zero for regular sequences). Default: 1");
    add_arg("--gbmaxcells", config.gbMaxCells, fc_integral<uint64_t>, "gb-cone with the F4/F5 engines: largest matrix (rows times columns) that is built; a step needing more stops the basis and, for a whole-system basis, makes gb-split split the system on a variable. Default: 2e9, 250 MB");
    add_arg("--gbsplit", config.gbSplitDepth, fc_integral<uint32_t>, "Rule gb-split: when the whole-system basis needs a matrix over --gbmaxcells, fix a variable both ways and combine the bases of the two branches, recursively up to this depth (0 = never). Fixing one variable of a random MQ system with n = 28 brings its degree of regularity from 5 back to 4. Default: 8");
    add_arg("--gbsplitrows", config.gbSplitRows, fc_integral<uint64_t>, "gb-split: matrix rows over all branches together, a deterministic work budget. Default: 2e7");
    add_arg("--gbtailreduce", config.gbTailReduce, fc_integral<int>, "gb-cone with the F4 engine: interreduce the tails of the basis after every degree step (no gain measured on MQ). Default: 0");
    add_arg("--gbf5groups", config.gbF5Groups, fc_integral<uint32_t>, "gb-cone with the F5 engine: generator groups per degree (more groups prune more rows but cost more eliminations). Default: 8");
    add_arg("--gbrecursion", config.gbRecursion, fc_integral<int>, "gb-cone with --gbfull 1: BRiAl's recursive implication bases for split generators (optAllowRecursion): 0 never, 1 always, 2 only for the whole-system basis of a small system (they cost 3-4x on small cones and are essential on MQ-like systems). Default: 2");
    add_arg("--gbwholevars", config.gbWholeVars, fc_integral<uint32_t>, "gb-cone with --gbfull 1: a system with at most this many free variables is taken as one cone and its complete Groebner basis computed (with gb-split when its matrices exceed --gbmaxcells); solves random MQ systems with up to ~32 variables outright; 0 = never. Default: 40");
    add_arg("--gbmaxlen", config.gbMaxLen, fc_integral<size_t>, "gb-cone: only equations with at most this many terms take part. Default: 32");
    add_arg("--gbsteps", config.gbSteps, fc_integral<uint64_t>, "gb-cone: S-polynomials reduced per call, a deterministic work budget. Default: 100000");
    add_arg("--gbfactdeg", config.gbFactDeg, fc_integral<uint32_t>, "gb-cone: add basis members of at most this degree. Default: 2");
    add_arg("--gbfactlen", config.gbFactLen, fc_integral<uint32_t>, "gb-cone: add basis members with at most this many terms. Default: 8");
    add_arg("--keepfactor", config.keepFactor, fc_integral<int>,
        "Never rewrite an equation that is a product of linear factors into one that is not, so the product form survives for the CNF encoding: 0 = off, 1 = on, 2 = auto (on when most nonlinear equations are such products). Default: 2");
    add_arg("--rewriterounds", config.rewriteRounds, fc_integral<uint32_t>,
        "Max rounds of the in-place rewrite rules per iteration. Default: 10");

    // XL
    add_arg("--xl", config.doXL, fc_integral<int>,
        "Turn on/off XL-based simplification. Default: ON");
    add_arg("--xldeg", config.xlDeg, fc_integral<uint32_t>,
        "Expansion degree for XL algorithm. Default = 1 (0 = Just GJE. For now we only support 0 <= xldeg = 3)");
    add_arg("--xlmaxlen", config.xlMaxLen, fc_integral<size_t>,
        "XL and ElimLin only see equations with at most this many terms (products of long linear factors have thousands and only cost time). Default: 64");
    add_arg("--xlsample", config.XLsample, fc_double,
        "Size of matrix to sample for XL, in log2");
    add_arg("--xlsamplex", config.XLsampleX, fc_double,
        "Size of matrix to sample for XL, in log2, that we can expand by");

    // ElimLin options
    add_arg("--el", config.doEL, fc_integral<int>,
        "Turn on/off ElimLin-based simplification. Default: ON");
    add_arg("--elsample", config.ELsample, fc_double,
        "Size of matrixto sample for EL, in log2");

    // SAT options
    add_arg("--sat", config.doSAT, fc_integral<int>,
        "Turn on/off SAT-based simplification. Default: ON");
    add_arg("--satinc", config.numConfl_inc, fc_integral<uint64_t>,
        "Conflict inc for built-in SAT solver.");
    add_arg("--satlim", config.numConfl_lim, fc_integral<uint64_t>,
        "Conflict limit for built-in SAT solver.");
    add_arg2("-t", "--threads", config.numThreads, fc_integral<unsigned int>,
        "Number of threads to use for SAT solver (same value is used for built-in and external).");
    add_str_arg("--solmap", solmap_file_write, "Write solution map to this file");
    /* clang-format on */

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception& err) {
        string msg = err.what();
        if (msg == "Duplicate argument") {
            std::map<string, int> seen;
            for (int i = 1; i < argc; i++) {
                if (argv[i][0] == '-') seen[argv[i]]++;
            }
            for (const auto& [k, v] : seen) {
                if (v > 1) msg += ": " + k;
            }
        }
        cerr << "ERROR parsing options: " << msg << endl
             << "Please give '--help' to get help" << endl;
        exit(-1);
    }

    // I/O checks
    readANF = program.is_used("--anfread");
    readCNF = program.is_used("--cnfread");

    // Positional input file: infer ANF/CNF from the extension
    const string posInput = program.get<string>("input");
    if (!posInput.empty()) {
        auto ends_with = [&](const string& suffix) {
            return posInput.size() >= suffix.size() &&
                   posInput.compare(posInput.size() - suffix.size(),
                                    suffix.size(), suffix) == 0;
        };
        if (ends_with(".anf")) {
            if (readANF) {
                cerr << "ERROR: input file given both as positional argument and via --anfread\n";
                exit(-1);
            }
            anfInput = posInput;
            readANF = true;
        } else if (ends_with(".cnf")) {
            if (readCNF) {
                cerr << "ERROR: input file given both as positional argument and via --cnfread\n";
                exit(-1);
            }
            cnfInput = posInput;
            readCNF = true;
        } else {
            cerr << "ERROR: cannot tell whether '" << posInput
                 << "' is ANF or CNF: it must end in .anf or .cnf, "
                    "or be given via --anfread/--cnfread\n";
            exit(-1);
        }
    }
    writeANF = program.is_used("--anfwrite");
    writeCNF = program.is_used("--cnfwrite");

    if (program.is_used("--solvewrite")) {
        solve_with_cms = true;
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
             << "c Rewrite rules: " << config.doRewrite
             << " (binom-red " << config.doBinomRed << " len " << config.binomRedLen
             << ", lin-gauss " << config.doLinGauss
             << ", poly-shorten " << config.doShorten
             << ", lit-probe " << config.doProbe << " vars " << config.probeVars << ")" << endl
             << "c XL simp (deg = " << config.xlDeg
             << "; s = " << config.XLsample << '+' << config.XLsampleX
             << "): " << config.doXL << endl
             << "c EL simp (s = " << config.ELsample << "): " << config.doEL
             << endl
             << "c SAT simp (" << config.numConfl_inc << ':'
             << config.numConfl_lim << "): " << config.doSAT << endl
             << " using " << config.numThreads << " threads" << endl
             << "c Cut num: " << config.cutNum << endl
             << "c Partner strategies: " << config.doPartner << endl
             << "c Linear-factor encoding: " << config.doFactor << " xor clauses: " << config.xorClauses << endl
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
        DIMACS* dimacs = mylib.parse_cnf(cnfInput.c_str());
        anf = mylib.chunk_dimacs(dimacs);
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
        mylib.simplify(anf, cnf_orig, maxiters);
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

    CNF* cnf = NULL;
    if (writeCNF) {
        if (!cnfInput.empty()) {
            const char* cnfInputTmp = cnfInput.c_str();
            if (only_new_cnf_clauses) {
                cnfInputTmp = NULL;
            }
            cnf = mylib.write_cnf(cnfInputTmp, cnfOutput.c_str(), anf);
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

            mylib.write_solution_map(cnf, &ofs);
            mylib.write_solution_map(anf, &ofs);
        }
    }

    if (solve_with_cms) {
        if (!writeCNF) cnf = mylib.write_cnf(NULL, anf);
        solve(&mylib, cnf, anf);
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

void print_solution_cnf_style(const Solution& solution);
void check_solution(const ANF* anf, const Solution& solution);
void print_solution_anf_style(const Solution& solution, Bosph::Bosphorus* mylib, const ANF* anf);
void clear_solution_file();
void write_solution_to_file_cnf_style(const Solution& solution);
void ban_solution(CMSat::SATSolver& solver, const Solution& solution, const std::set<size_t>& proj);


Solution extend_solution(
    const vector<CMSat::lbool>& model,
    const std::map<uint32_t, VarMap>& varmap,
    uint32_t num_anf_vars
);

void solve(Bosph::Bosphorus* mylib, CNF* cnf, ANF* anf) {
    vector<Clause> cls = mylib->get_clauses(cnf);
    CMSat::SATSolver solver;
    solver.set_num_threads(config.numThreads);
    // The settings for XOR-heavy systems (like "cryptominisat5 --sls 0
    // --autodisablegauss 0 --presimp 1 --maxnummatrices 1000000
    // --minmatrixrows 1"): Gauss-Jordan elimination is never disabled
    // automatically, XORs are recovered from the clauses and every matrix
    // is kept, however small.
    solver.set_sls(0);
    solver.set_allow_otf_gauss();
    solver.set_simplify_at_startup(1);
    solver.set_find_xors(true);
    solver.set_max_num_matrices(1000000);
    solver.set_min_matrix_rows(1);
    solver.new_vars(mylib->get_max_var(cnf));
    for(const Bosph::Clause& c: cls) {
        const Bosph::Clause* cc = &c;
        const vector<CMSat::Lit>* cc2 = (vector<CMSat::Lit>*)cc;
        solver.add_clause(*cc2);
    }
    for (const auto& x : mylib->get_xor_clauses(cnf)) {
        solver.add_xor_clause(x.first, x.second);
    }

    clear_solution_file();
    uint32_t number_of_solutions = 0;
    while(true) {
        CMSat::lbool ret = solver.solve();
        Solution solution;
        if (ret == CMSat::l_True) {
            solution.ret = l_True;
            std::map<uint32_t, VarMap> varmap;
            mylib->get_solution_map(anf, varmap);
            mylib->get_solution_map(cnf, varmap);
            uint32_t num_anf_vars = mylib->get_max_var(anf);
            solution = extend_solution(solver.get_model(), varmap, num_anf_vars);
        } else {
            solution.ret = l_False;
        }
        print_solution_anf_style(solution, mylib, anf);
        write_solution_to_file_cnf_style(solution);
        if (ret == CMSat::l_True) {
            check_solution(anf, solution);
        }
        if (ret == CMSat::l_False) break;
        number_of_solutions++;
        if (!all_solutions && max_sol <= number_of_solutions) {
            break;
        }
        ban_solution(solver, solution, mylib->get_proj_set(anf));
    }

    if (all_solutions || max_sol > 1) {
        cout << "c Number of solutions found: " << number_of_solutions << endl;
    }
}

void clear_solution_file()
{
    if (solution_output_file.empty()) {
        return;
    }

    std::ofstream ofs;
    ofs.open(solution_output_file);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << solution_output_file
                  << "\" for writing\n";
        exit(-1);
    }
    ofs.close();
}

void write_solution_to_file_cnf_style(const Solution& solution)
{
    if (solution_output_file.empty()) {
        return;
    }

    std::ofstream ofs;
    ofs.open(solution_output_file, std::ios_base::app);
    if (!ofs) {
        std::cerr << "c Error opening file \"" << solution_output_file
                  << "\" for writing\n";
        exit(-1);
    }
    assert(solution.ret != l_Undef);
    ofs << "Solution ";
    ofs << ((solution.ret == l_True) ? "SAT" : "UNSAT") << endl;
    if (solution.ret == l_False) {
        return;
    }


    size_t num = 0;
    ofs << "v ";
    for (const lbool lit : solution.sol) {
        if (lit != l_Undef) {
            ofs << ((lit == l_True) ? "" : "-") << num << " ";
        }
        num++;
    }
    ofs << endl;
}

void ban_solution(CMSat::SATSolver& solver, const Solution& solution, const std::set<size_t>& proj)
{
    vector<CMSat::Lit> clause;
    for(uint32_t i = 0; i < solution.sol.size(); i++) {
        if (proj.find(i) == proj.end()) continue;
        if (solution.sol[i] != l_Undef) {
            auto lit = CMSat::Lit(i, solution.sol[i] == l_True);
            clause.push_back(lit);
        }
    }
    solver.add_clause(clause);
}

void print_solution_anf_style(const Solution& s, Bosph::Bosphorus* mylib, const ANF* anf)
{
    if (s.ret == l_False) {
        cout << "s ANF-UNSATISFIABLE" << endl;
        return;
    }

    assert(s.ret == l_True);
    cout << "s ANF-SATISFIABLE" << endl;
    cout << "v ";
    for(uint32_t i = 0; i < s.sol.size(); i++) {
        if (s.sol[i] != l_Undef) {
            if (s.sol[i] == l_True) {
                cout << "1+";
            }
            cout << mylib->get_var_name(anf, i) << ' ';
        }
    }
    cout << endl;
}

Solution extend_solution(
    const vector<CMSat::lbool>& model,
    const std::map<uint32_t, VarMap>& varmap,
    uint32_t num_anf_vars
) {
    Solution s;
    s.ret = l_True;
    s.sol.resize(num_anf_vars);
    for(auto& x: s.sol) x = l_Undef;

    for(int do_must_set = 0; do_must_set < 2; do_must_set++) {
        bool changed = true;
        while(changed) {
            changed = false;
            for(const auto& v: varmap) {
                if (v.first > s.sol.size()) continue;
                if (s.sol[v.first] == l_Undef) {
                    switch (v.second.type) {
                        case Bosph::VarMap::fixed:
                            s.sol[v.first] = v.second.value ? l_True : l_False;
                            changed = true;
                            break;
                        case Bosph::VarMap::cnf_var:
                            s.sol[v.first] = (model[v.second.other_var] == CMSat::l_True) ? l_True: l_False;
                            changed = true;
                            break;
                        case Bosph::VarMap::anf_repl:
                            if (s.sol[v.second.other_var] != l_Undef) {
                                s.sol[v.first] = s.sol[v.second.other_var] ^ v.second.inv;
                                changed = true;
                            }
                            break;
                        case Bosph::VarMap::must_set:
                            if (do_must_set) {
                                s.sol[v.first] = l_True;
                                changed = true;
                            }
                            break;
                    }
                }
            }
        }
    }
    return s;
}

void check_solution(const ANF* anf, const Solution& solution)
{
    //Checking
    bool goodSol = Bosphorus::evaluate(anf, solution.sol);
    if (!goodSol) {
        cout << "ERROR! Solution found is incorrect!" << endl;
        exit(-1);
    }
    if (config.verbosity) {
        cout << "c Solution found is correct." << endl;
    }
}
