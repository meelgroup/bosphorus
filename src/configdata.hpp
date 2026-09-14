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

#ifndef CONFIGDATA__H
#define CONFIGDATA__H

#include <limits>
#include <string>
#include <cstdint>

using std::string;

namespace BLib {

struct ConfigData {
    // Input/Output
    string executedArgs = "";
    bool writecomments = false;
    int projShow = 2; // 'c p show' projection line in the CNF: 0 never, 1 always, 2 when the input had a projection set
    bool printProcessedANF = false;
    uint32_t verbosity = 2;
    int color = 2; // 0 = never, 1 = always, 2 = auto (tty && !NO_COLOR)
    int simplify = 1;

    // CNF conversion
    uint32_t cutNum = 5;
    uint32_t brickestein_algo_cutoff = 10;
    uint32_t karnCluster = 10; // jointly encode small nonlinear eqs sharing variables, up to this many vars (0 = off)
    int doPartner = true; // Jovanovic-Kreuzer partner strategies (LPS/DPS/QPS/CPS)
    int doFactor = true;   // encode polynomials that are products of linear factors as one clause over the factors
    int xorClauses = false; // emit XORs as native CryptoMiniSat xor clauses instead of cutting them
    uint32_t xorMaxLen = 0;  // with xorClauses: cut native XORs longer than this into native pieces (0 = never)

    // Processes
    double maxTime = 1e20;
    int doXL = true;
    int doEL = true;
    int doSAT = true;
    double XLsample = 30.0;
    size_t xlMaxLen = 64; // XL and ElimLin only see equations with at most this many terms
    double XLsampleX = 4.0;
    double ELsample = 30.0;
    uint32_t xlDeg = 1;
    uint64_t numConfl_inc = 10000;
    uint64_t numConfl_lim = 100000;
    unsigned int numThreads = 1;

    // In-place ANF rewrite rules
    int doRewrite = true;         // master switch
    int doLinGauss = true;        // Gaussian elimination among the linear equations
    int spanFilter = true;        // drop learnt linear facts that are combinations of existing linear equations
    int doBinomRed = true;        // reduction modulo monomial/binomial eqs
    uint32_t binomRedLen = 2;     // max terms of an equation used as a rule
    int doShorten = true;         // p -> p + f when that has fewer terms
    size_t shortenOccCap = 2000;  // ignore monomials in more eqs than this
    int64_t shortenBudget = 200000000; // work limit per call
    int doProbe = true;           // forced literals/equivalences/implications
    uint32_t probeVars = 8;       // only probe eqs with at most this many vars
    uint32_t rewriteRounds = 10;  // max rounds of all rules per call
    int doFacCanon = false;       // canonicalise linear factors modulo the linear equations
    int doFacRes = false;         // resolution between products sharing a factor
    uint32_t facResMaxFactors = 2; // resolvents with more factors than this are not added
    // cones of small equations, degree-bounded Groebner bases (rule gb-cone)
    int doGB = false;
    uint32_t gbDeg = 3;        // S-polynomials above this degree are dropped
    uint32_t gbWindow = 24;    // equations per window
    size_t gbMaxLen = 32;      // only equations with at most this many terms take part
    uint64_t gbSteps = 100000; // S-polynomials reduced per call (deterministic budget)
    uint32_t gbMaxVars = 16;   // a cone grows while its equations use at most this many variables
    int gbFull = 2;            // 0: degree-bounded lex loop per cone; 1: complete dp_asc basis per cone; 2: both for cones, complete basis for a whole small system
    uint32_t gbWholeVars = 24; // with gbFull: a system with at most this many free variables is one cone
    int gbRecursion = 2;       // BRiAl's optAllowRecursion: 0 never, 1 always, 2 only for the whole-system basis
    int gbEngine = 1;          // complete bases: 0 = BRiAl symmGB_F2, 1 = Bosphorus's matrix F4 (boolf4.cpp)
    uint64_t gbMaxCells = 16000000000ULL; // F4: largest matrix (rows*columns) that is built, ~2 GB of bits
    int gbTailReduce = false;  // F4: interreduce the basis tails after every degree step (no gain measured)
    uint32_t gbFactDeg = 2;    // GB members of at most this degree ...
    uint32_t gbFactLen = 8;    // ... and this many terms are added as facts
    int keepFactor = 2;           // never rewrite a product of linear factors into a non-product: 0 off, 1 on, 2 auto (when most nonlinear eqs are such products)
};

}

#endif //CONFIGDATA__H
