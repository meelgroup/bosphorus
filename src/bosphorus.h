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

#ifndef MYLIBRARY_H_
#define MYLIBRARY_H_

#include <vector>

#include "GitSHA1.h"
#include "anf.hpp"
#include "cnf.hpp"
#include "dimacscache.hpp"
#include "gaussjordan.hpp"
#include "replacer.hpp"
#include "time_mem.h"
#include "solution.h"

// ALGOS
#include "elimlin.hpp"
#include "extendedlinearization.hpp"
#include "simplifybysat.hpp"

using std::vector;

class Bosphorus
{
public:
    ~Bosphorus();
    void set_config(const ConfigData& cfg);
    static const char* get_compilation_env();
    static const char* get_version_tag();
    static const char* get_version_sha1();

    // To read CNF or ANF from file
    ANF* read_anf(const char* fname);
    ANF* read_cnf(const char* fname);

    // To insert dynamically generated CNF
    ANF* start_cnf_input(uint32_t max_vars);
    void add_clause(ANF* anf, const std::vector<int>& clause);

    // Output functions
    void write_anf(const char* fname, const ANF* anf);
    void write_cnf(const char* input_cnf_fname,
              const char* output_cnf_fname,
              const ANF* anf);


    CNF* anf_to_cnf(const ANF* anf);
    CNF* cnf_from_anf_and_cnf(const char* cnf_fname, const ANF* anf);

    Solution simplify(ANF* anf, const char* orig_cnf_file);

    void deduplicate();
    void add_trivial_learnt_from_anf_to_learnt(
        ANF* anf,
        const ANF::eqs_hash_t& orig_eqs_hash);


    size_t get_learnt_size() const {
        return learnt.size();
    }

    vector<Clause> get_learnt(ANF* anf);

private:
    void check_library_in_use();

    ConfigData config;
    BoolePolyRing* polybori_ring = nullptr;
    vector<Clause> extra_clauses;
    vector<BoolePolynomial> learnt;

    bool read_in_data = false;
};

#endif
