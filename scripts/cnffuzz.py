#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (C) 2014  Mate Soos
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; version 2
# of the License.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
# 02110-1301, USA.

import subprocess
import random
import optparse

def myexec(command):
    print("Executing command: " , command)
    p = subprocess.Popen(command.rsplit(), stderr=subprocess.STDOUT,
            stdout=subprocess.PIPE, universal_newlines=True)

    console_out, err = p.communicate()

    if err != None:
        print("Error, fuzzer didn't work, err: ", err)
        print("console output:", out)
        exit(-1)

    return console_out, err

def get_num_vars(fname):
    with open(fname, "r") as f:
        for line in f:
            line = line.strip()
            if len(line) == 0:
                continue

            if line[0] != "p":
                continue

            line = line.split()
            assert line[1].strip() == "cnf"
            return int(line[2])


def get_new_clauses(fname, orig_num_vars = 0):
    clauses = []
    definitions = []
    with open(fname, "r") as f:
        for line in f:
            line = line.strip()
            if len(line) == 0:
                continue

            if line[0] == "c":
                continue

            if line[0] == "p":
                line = [x.strip() for x in line.split()]
                assert line[1] == "cnf"
                num_vars = int(line[2])
                num_cls = int(line[3])
                continue

            line = [int(x) for x in line.split()]
            clause = []
            definition = False
            for l in line:
                if l == 0:
                    break
                if abs(l) > orig_num_vars:
                    definition = True
                clause.append(l)

            if definition:
                definitions.append(clause)
            else:
                clauses.append(clause)

    return definitions, clauses


def find_max_var(cls):
    max_var = 0
    for cl in cls:
        for l in cl:
            var = abs(l)
            max_var = max(var, max_var)

    return max_var

def create_cnf(orig_fname, fname, new_facts, new_defs):
    cls, x = get_new_clauses(orig_fname)
    assert len(x) == 0

    max_var = find_max_var(cls+new_defs)

    fullcls = cls+new_defs+new_facts
    with open(fname, "w") as f:

        f.write("p cnf %d %d\n" % (max_var, len(fullcls)))
        for cl in fullcls:
            towrite = ""
            for l in cl:
                towrite += "%d " % l
            f.write(towrite+"0\n")

def check_problem(problem_file):
    # get implied clauses
    command = "./bosphorus --cnfread %s --cnfwrite out2.cnf --comments 1 --maxiters 1" % problem_file
    command = "./bosphorus --cnfread %s --cnfwrite out2.cnf --comments 1 --onlynewcnfcls 1 --maxiters 1" % problem_file
    out, err = myexec(command)
    for l in out.split("\n"):
        if "anf-to-cnf" in l:
            print(l)

    # get orig num vars
    orig_num_vars = get_num_vars(problem_file)
    print("orig num vars:", orig_num_vars)

    # get new clauses and definitions
    defs, cls = get_new_clauses("out2.cnf", orig_num_vars)
    print("defs:", defs)
    print("cls:", cls)

    # verify new clauses are valid
    for cl in cls:
        print("Checking if clause %s is implied" % cl)
        new_facts = []
        for l in cl:
            assert l != 0
            new_facts.append([-l])

        create_cnf(problem_file, "out3.cnf", new_facts, defs)
        command = "lingeling out3.cnf"
        out, err = myexec(command)
        if "s UNSATISFIABLE" not in out:
            print("ERROR: %s is not implied" % cl)
            exit(-1)
        else:
            print("OK, UNSAT, so clause implied")


def one_fuzz_run(seed):
    print("Running with seed: %d" % seed)

    problem_file = "out.cnf"
    command = "../utils/cnf-utils/cnf-fuzz-brummayer.py"
    command += " -s %d" % seed
    out, err = myexec(command)

    with open(problem_file, "w") as f:
        f.write(out)

    print("OK, %s written" % problem_file)

    check_problem(problem_file)


class PlainHelpFormatter(optparse.IndentedHelpFormatter):

    def format_description(self, description):
        if description:
            return description + "\n"
        else:
            return ""

if __name__ == "__main__":
    usage = "usage: %prog [options]"
    desc = """Fuzz the system with fuzz-generator: ./fuzz_test.py
    """

    parser = optparse.OptionParser(usage=usage, description=desc,
                                   formatter=PlainHelpFormatter())

    # 9 gives us 1 extra variable
    # 8 gives us 0 extra variable
    parser.add_option("--seed", dest="fuzz_seed_start", default=8,
                      help="Fuzz test start seed", type=int)
    parser.add_option("--onerun", dest="one_seed", default=-1,
                      help="Only run one, with specified seed", type=int)
    (options, args) = parser.parse_args()

    if len(args) > 1:
        print("ERROR: Max one argument can be given, the problem file")
        exit(-1)

    if len(args) == 1:
        check_problem(args[0])
        exit(0)

    if options.one_seed != -1:
        one_fuzz_run(options.one_seed)
    else:
        random.seed(options.fuzz_seed_start)
        for x in range(10000):
            seed = random.randint(1, 1000000)
            one_fuzz_run(seed)
