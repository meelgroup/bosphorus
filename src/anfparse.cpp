/*****************************************************************************
Copyright (C) 2016  Security Research Labs
Copyright (C) 2018  Mate Soos, Davin Choo, Kian Ming A. Chai, DSO National Laboratories
Copyright (C) 2026  Mate Soos

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

// The ANF file reader.
//
// A line is a polynomial: terms separated by '+', a term is a product of
// variables separated by '*' or the constant 0 or 1. A variable is x<N> or
// x(N) (index N), or any other name made of letters, digits and '_',
// optionally followed by an index in square brackets such as K[3] or
// sbox_in[1,65]; such names get indices in order of first appearance. A line
// that is a comma separated list of variables declares them and is not an
// equation. Lines starting with 'c' are comments; "c p show v1 v2 ... END"
// gives the projection set. Text after a comma in an equation line is a
// description and ignored.

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

#include "anf.hpp"

using std::cout;
using std::endl;
using std::string;
using namespace BLib;

namespace {

enum TokType { T_VAR_NUM, T_VAR_NAME, T_PLUS, T_STAR, T_COMMA, T_CONST0, T_CONST1 };

struct Tok {
    TokType t;
    uint32_t num = 0;   // T_VAR_NUM: the index
    uint32_t start = 0; // T_VAR_NAME: the name is line.substr(start, len)
    uint32_t len = 0;
};

[[noreturn]] void parse_error(const string& msg, const string& line)
{
    cout << "ERROR: " << msg << " in line: \"" << line << "\"" << endl;
    exit(-1);
}

void tokenize(const string& line, std::vector<Tok>& out)
{
    out.clear();
    size_t i = 0;
    const size_t n = line.size();
    // Brackets may enclose a single term, as in "(x3) + x1" or "(x1*x2)";
    // "(x1 + x2)*x3" is a factorised expression, not ANF, and rejected.
    int depth = 0;
    while (i < n) {
        const char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '(') {
            if (depth > 0) parse_error("nested brackets", line);
            depth++; i++; continue;
        }
        if (c == ')') {
            if (depth == 0) parse_error("close bracket without an open one", line);
            depth--; i++; continue;
        }
        if (c == '+') {
            if (depth > 0) parse_error("'+' inside brackets: factorised expressions are not ANF", line);
            out.push_back({T_PLUS}); i++; continue;
        }
        if (c == '*') { out.push_back({T_STAR}); i++; continue; }
        if (c == ',') { out.push_back({T_COMMA}); i++; continue; }
        if (std::isdigit((unsigned char)c)) {
            size_t j = i;
            while (j < n && std::isdigit((unsigned char)line[j])) j++;
            const string num = line.substr(i, j - i);
            if (num == "0") out.push_back({T_CONST0});
            else if (num == "1") out.push_back({T_CONST1});
            else parse_error("a number that is not a variable (variables are x<N>, x(N) or names)", line);
            i = j;
            continue;
        }
        if (std::isalpha((unsigned char)c) || c == '_') {
            // fast path for x<N>, by far the most common token in big files:
            // no string is built
            if ((c == 'x' || c == 'X') && i + 1 < n && std::isdigit((unsigned char)line[i + 1])) {
                size_t j = i + 1;
                uint64_t num = 0;
                while (j < n && std::isdigit((unsigned char)line[j])) {
                    num = num * 10 + (line[j] - '0');
                    if (num > std::numeric_limits<uint32_t>::max()) parse_error("variable index too large", line);
                    j++;
                }
                if (j == n || !(std::isalnum((unsigned char)line[j]) || line[j] == '_' || line[j] == '[')) {
                    Tok t;
                    t.t = T_VAR_NUM;
                    t.num = num;
                    out.push_back(std::move(t));
                    i = j;
                    continue;
                }
                // x12ab or x1[..]: a named variable, handled below
            }
            size_t j = i;
            while (j < n && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) j++;
            const size_t name_start = i;
            string name = line.substr(i, j - i);
            i = j;
            // x(N): the old bracket form of a numbered variable
            if ((name == "x" || name == "X") && i < n && line[i] == '(') {
                size_t k = i + 1;
                while (k < n && std::isdigit((unsigned char)line[k])) k++;
                if (k == i + 1 || k >= n || line[k] != ')') parse_error("malformed x(N)", line);
                Tok t;
                t.t = T_VAR_NUM;
                t.num = std::stoul(line.substr(i + 1, k - i - 1));
                out.push_back(t);
                i = k + 1;
                continue;
            }
            // x<N>: a numbered variable
            if ((name[0] == 'x' || name[0] == 'X') && name.size() > 1 &&
                std::all_of(name.begin() + 1, name.end(), [](char d) { return std::isdigit((unsigned char)d); })) {
                Tok t;
                t.t = T_VAR_NUM;
                t.num = std::stoul(name.substr(1));
                out.push_back(t);
                continue;
            }
            // a named variable, optionally with an index in brackets
            if (i < n && line[i] == '[') {
                size_t k = line.find(']', i);
                if (k == string::npos) parse_error("unclosed '[' in variable name", line);
                name += line.substr(i, k - i + 1);
                i = k + 1;
            }
            if (name == "x" || name == "X") parse_error("x is not followed by a number", line);
            Tok t;
            t.t = T_VAR_NAME;
            t.start = name_start;
            t.len = i - name_start;
            out.push_back(t);
            continue;
        }
        parse_error(string("unknown character '") + c + "'", line);
    }
    if (depth != 0) parse_error("unclosed bracket", line);
}

bool is_var(const Tok& t) { return t.t == T_VAR_NUM || t.t == T_VAR_NAME; }

// "v1, v2, v3": a declaration of variables, not an equation
bool is_declaration(const std::vector<Tok>& toks)
{
    bool has_comma = false;
    for (size_t i = 0; i < toks.size(); i++) {
        if (i % 2 == 0) { if (!is_var(toks[i])) return false; }
        else { if (toks[i].t != T_COMMA) return false; has_comma = true; }
    }
    return has_comma && toks.size() % 2 == 1;
}

}

ANF::Names ANF::scanFile(const string& filename)
{
    Names names;
    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        cout << "Problem opening file: \"" << filename << "\" for reading\n";
        exit(-1);
    }
    long max_num = -1;
    std::vector<string> order; // named variables in order of first appearance
    std::unordered_set<string> seen;
    std::vector<Tok> toks;
    string line;
    while (std::getline(ifs, line)) {
        if (line.empty() || line[0] == 'c') continue;
        tokenize(line, toks);
        for (const Tok& t : toks) {
            if (t.t == T_VAR_NUM) max_num = std::max<long>(max_num, t.num);
            else if (t.t == T_VAR_NAME) {
                const string name = line.substr(t.start, t.len);
                if (seen.insert(name).second) order.push_back(name);
            }
        }
    }
    // x<N> variables keep their index; names follow after the highest one
    const size_t base = max_num + 1;
    names.ring_size = base + order.size();
    if (names.ring_size == 0) names.ring_size = 1; // x0 always exists
    names.names.assign(names.ring_size, "");
    for (size_t i = 0; i < order.size(); i++) {
        names.names[base + i] = order[i];
        names.index[order[i]] = base + i;
    }
    return names;
}

size_t ANF::readFile(const string& filename, const Names* names)
{
    Names local;
    if (names == nullptr) {
        local = scanFile(filename);
        names = &local;
    }
    std::ifstream ifs(filename.c_str());
    if (!ifs) {
        cout << "Problem opening file: \"" << filename << "\" for reading\n";
        exit(-1);
    }
    auto var_index = [&](const Tok& t, const string& line) -> uint32_t {
        if (t.t == T_VAR_NUM) return t.num;
        const string name = line.substr(t.start, t.len);
        auto it = names->index.find(name);
        if (it == names->index.end()) parse_error("unknown variable " + name, line);
        return it->second;
    };

    size_t maxVar = 0;
    bool proj_set_found = false;
    std::vector<Tok> toks;
    std::vector<VarVec> terms; // reused from line to line
    string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;

        if (line[0] == 'c') {
            comments.push_back(line);
            std::istringstream iss(line);
            string txt;
            iss >> txt;
            if (txt != "c") continue;
            iss >> txt;
            if (txt != "p") continue;
            iss >> txt;
            if (txt != "show") continue;
            if (proj_set_found) {
                cout << "ERROR: more than one 'c p show' (projection set) line" << endl;
                exit(-1);
            }
            proj_set_found = true;
            string rest;
            std::getline(iss, rest);
            size_t end = rest.find("END");
            if (end == string::npos) {
                cout << "ERROR: the projection set line must end with END" << endl;
                exit(-1);
            }
            tokenize(rest.substr(0, end), toks);
            for (const Tok& t : toks) {
                if (!is_var(t)) parse_error("the projection set may only list variables", line);
                const uint32_t v = var_index(t, line);
                if (!proj_set.insert(v).second) {
                    cout << "ERROR: variable listed twice in the projection set: " << line << endl;
                    exit(-1);
                }
            }
            continue;
        }

        tokenize(line, toks);
        if (toks.empty()) continue;
        if (is_declaration(toks)) {
            for (const Tok& t : toks) {
                if (is_var(t)) maxVar = std::max<size_t>(maxVar, var_index(t, line));
            }
            if (config.verbosity >= 2) {
                cout << "c [ANF Input] skipping variable declaration line" << endl;
            }
            continue;
        }
        // text after a comma is a description: ignored
        size_t len = toks.size();
        for (size_t i = 0; i < toks.size(); i++) {
            if (toks[i].t == T_COMMA) { len = i; break; }
        }

        // terms separated by '+'; the term vectors are reused from line to
        // line (big files have thousands of terms per equation)
        size_t nterms = 0;
        auto new_term = [&]() -> VarVec& {
            if (nterms == terms.size()) terms.emplace_back();
            VarVec& t = terms[nterms++];
            t.clear();
            return t;
        };
        size_t i = 0;
        while (i < len) {
            if (toks[i].t == T_CONST0 || toks[i].t == T_CONST1) {
                if (toks[i].t == T_CONST1) new_term();
                i++;
            } else if (is_var(toks[i])) {
                VarVec& term = new_term();
                term.push_back(var_index(toks[i], line));
                i++;
                while (i < len && toks[i].t == T_STAR) {
                    if (i + 1 >= len || !is_var(toks[i + 1])) parse_error("'*' not followed by a variable", line);
                    term.push_back(var_index(toks[i + 1], line));
                    i += 2;
                }
                for (const uint32_t v : term) maxVar = std::max<size_t>(maxVar, v);
            } else {
                parse_error("a term must start with a variable, 0 or 1", line);
            }
            if (i < len) {
                if (toks[i].t != T_PLUS) parse_error("terms must be separated by '+'", line);
                i++;
                if (i >= len) parse_error("'+' at the end of the line", line);
            }
        }
        terms.resize(nterms);
        addTerms(terms);
    }

    for (const auto& v : proj_set) {
        if (v >= ring->nVariables()) {
            cout << "ERROR: the projection set contains a variable outside the ring" << endl;
            exit(-1);
        }
    }
    if (!proj_set_found) {
        cout << "c setting projection set to ALL variables since we didn't find a 'c p show ... END'" << endl;
        for (uint32_t i = 0; i < ring->nVariables(); i++) proj_set.insert(i);
    }
    return maxVar;
}
