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
#include <map>
#include <fstream>
#include "bosphorus/solvertypesmini.hpp"

using std::vector;

class PrivateData;
class ANF;
class CNF;

namespace Bosph {

class Bosphorus
{
public:
    ~Bosphorus();
    Bosphorus();
    void set_config(void* cfg);
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
    CNF* write_cnf(const char* output_cnf_fname, const ::ANF* a);
    CNF* write_cnf(
        const char* input_cnf_fname,
        const char* output_cnf_fname,
        const ANF* anf);

    void write_solution_map(CNF* cnf, std::ofstream* ofs);
    void write_solution_map(ANF* anf, std::ofstream* ofs);
    void get_solution_map(const ANF* a, std::map<uint32_t, VarMap>& ret) const;
    void get_solution_map(const CNF* c, std::map<uint32_t, VarMap>& ret) const;


    CNF* anf_to_cnf(const ANF* anf);
    CNF* cnf_from_anf_and_cnf(const char* cnf_fname, const ANF* anf);
    uint32_t get_max_var(const CNF* cnf) const;
    uint32_t get_max_var(const ANF* anf) const;

    bool simplify(ANF* anf, const char* orig_cnf_file, uint32_t max_iters = 100);

    void deduplicate();
    void add_trivial_learnt_from_anf_to_learnt(
        ANF* anf,
        ANF* other);

    size_t get_learnt_size() const;
    static void delete_anf(ANF* anf);
    static bool evaluate(const ANF* anf, const vector<lbool>& sol);
    static void print_stats(ANF* anf);
    static ANF* copy_anf_no_replacer(ANF* anf);
    static void print_anf(ANF* a);

    vector<Clause> get_clauses(CNF* cnf);
    vector<Clause> get_learnt(ANF* anf);

private:
    void check_library_in_use();

    PrivateData* dat = NULL;
};

}

#endif
