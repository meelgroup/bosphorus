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

from __future__ import with_statement  # Required in 2.5
from __future__ import print_function
import sys

def parse_solution(fname):
    num_vars = 0
    solution = {}
    unsat = None
    with open(fname, "r") as f:
        for line in f:
            line = line.strip()
            if line == "s UNSATISFIABLE":
                return True, None

            if line ==  "s SATISFIABLE":
                unsat = False

            vline = False
            for elem in line.split(" "):
                elem = elem.strip()
                if elem == "v":
                    vline = True
                    continue
                if not vline:
                    continue

                try:
                    x = int(elem)
                except ValueError:
                    print("ERROR: Solution has a line starting with 'v' that contains a non-integer: ", elem)
                    exit(-1)

                val = None
                if x < 0:
                    val = False
                else:
                    val = True

                var = abs(x)
                if var == 0:
                    # end of line
                    continue

                if var in solution:
                    print("ERROR: Solution contains the variable %d twice" % var)
                    exit(-1)

                assert var > 0, "ERROR in solution parsing"
                var = var-1;
                solution[var] = val
                num_vars += 1

    if num_vars == 0:
        print("ERROR: no solution found in solution file '%s'" % fname)
        exit(-1)

    if unsat is None:
        print("ERROR: solution neither says UNSAT or SAT, maybe things didn't get solved?")
        exit(-1)

    return unsat, solution

if len(sys.argv) != 3:
    print("ERROR: You must give 2 arguments: map file and solution file")
    print("Usage: map_solution.py map_file solution_file")
    exit(-1)


mapfile = sys.argv[1]
solfile = sys.argv[2]

print("c solution file: ", solfile)
print("c map file: ", mapfile)

# parse up solution
unsat, cnfsol = parse_solution(solfile)
if unsat:
    print("s UNSATISFIABLE")
    exit(0)
# print ("CNF solution: ", cnfsol)


anfsol = {}
with open(mapfile, "r") as f:
    for line in f:
        line = line.strip()
        line = line.split()
        if len(line) == 0:
            continue

        if line[0] == "Internal-ANF-var":
            assert len(line) == 5, "Map file corrupted!"
            try:
                var = int(line[4])
            except ValueError:
                print("ERROR: Map file is corrupt, cannot parse variable: ", line[4])
                print("Tried parsing line:", line)
                exit(-1)

            assert var >= 0
            anfsol[var] = cnfsol[var]

        elif line[0] == "ANF-var-val":
            try:
                var = int(line[1])
            except ValueError:
                print("ERROR: Map file is corrupt, cannot parse variable: ", line[3])
                print("Tried parsing line:", line)
                exit(-1)

            val = None
            if line[3] == "l_True":
                val = True
            elif line[3] == "l_False":
                val = False
            else:
                print("ERROR: Map file is corrupt, cannot parse: ", line[3])
                print("Tried parsing line:", line)
                exit(-1)

            anfsol[var] = val

        elif line[0] == "must-set-ANF-var-to-any":
            try:
                var = int(line[1])
            except ValueError:
                print("ERROR: Map file is corrupt, cannot parse variable: ", line[1])
                print("Tried parsing line:", line)
                exit(-1)

            # any value works, so setting to TRUE
            anfsol[var] = True

        elif line[0] == "ANF-var":
            try:
                var1 = int(line[1])
                var2 = int(line[4])
                inv = int(line[6])
            except ValueError:
                print("ERROR: Map file is corrupt")
                print("Tried parsing line:", line)
                exit(-1)

            assert line[5] == "^", "Map file is corrupt"
            assert var1 >= 0, "Map file is corrupt"
            assert var2 >= 0, "Map file is corrupt"
            assert inv == 0 or inv == 1, "Map file is corrupt"

            val = int(anfsol[var2]) ^ inv
            if val == 0:
                realval = False
            elif val == 1:
                realval = True
            else:
                assert False

            anfsol[var1] = realval

        else:
            print("Map file corrupt, cannot understand line: ", line)

# print("ANF solution:", anfsol)
sol_in_txt = ""
for a,b in anfsol.items():
    extra = ""
    if not b:
        extra = "-"
    sol_in_txt += "%s%d " %(extra, a)

print("c solution below, with variables starting at 0, as per ANF convention.")
print("c preceding '-' means it's False, so '-0' has a meaning")
print("s ANF-SATISFIABLE")
print("v %s" % sol_in_txt)
