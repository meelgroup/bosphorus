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

def myexec(command):
    print("Executing command: " , command)
    p = subprocess.Popen(command.rsplit(), stderr=subprocess.STDOUT,
            stdout=subprocess.PIPE, universal_newlines=True)

    console_out, err = p.communicate()

    return console_out, err

def get_num_vars(fname):
    with open("out2.cnf", "r") as f:
        for line in f:
            line = line.strip()
            if len(line) == 0:
                continue

            if line[0] != "p":
                continue

            line = line.split()
            assert line[1].strip() == "cnf"
            return int(line[2])

def get_new_clauses(fname, orig_num_vars):
    clauses = []
    definitions = []
    with open("out2.cnf", "r") as f:
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

if __name__ == "__main__":

    random.seed(0);

    command = "../utils/cnf-utils/cnf-fuzz-brummayer.py"
    command += " -s %d" % random.randint(1, 1000000)

    out, err = myexec(command)
    if err != None:
        print("Error, fuzzer didn't work, err: ", err)
        print("console output:", out)
        exit(-1)

    with open("out.cnf", "w") as f:
        f.write(out)

    print("OK, out.cnf written")

    command = "./bosphorus --cnfread out.cnf --cnfwrite out2.cnf --comments 1"
    command = "./bosphorus --cnfread out.cnf --cnfwrite out2.cnf --comments 1 --onlynewcnfcls 1"
    out, err = myexec(command)
    if err != None:
        print("Error, fuzzer didn't work, err: ", err)
        print("console output:", out)
        exit(-1)

    # get orig num vars
    orig_num_vars = get_num_vars("out.cnf")
    print("orig num vars:", orig_num_vars)

    # get new clauses and definitions
    defs, cls = get_new_clauses("out2.cnf", orig_num_vars)
    print("defs:", defs)
    print("cls:", cls)



