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
    vline_found = False
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
                    vline_found = True
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

    if not vline_found:
        print("ERROR: no solution found in solution file '%s'" % fname)
        exit(-1)

    if unsat is None:
        print("ERROR: solution neither says UNSAT or SAT, maybe things didn't get solved?")
        exit(-1)

    return unsat, solution


def parse_one_map_line(line, anf_map):
    line = line.strip()
    line = line.split()
    if len(line) == 0:
        return

    if line[0] == "Internal-ANF-var":
        assert len(line) == 5, "Map file corrupted!"
        try:
            var = int(line[4])
        except ValueError:
            print("ERROR: Map file is corrupt, cannot parse variable: ", line[4])
            print("Tried parsing line:", line)
            exit(-1)

        assert var >= 0
        anf_map[var] = ("cnf", var)

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

        anf_map[var] = ("set", val)

    elif line[0] == "must-set-ANF-var-to-any":
        try:
            var = int(line[1])
        except ValueError:
            print("ERROR: Map file is corrupt, cannot parse variable: ", line[1])
            print("Tried parsing line:", line)
            exit(-1)

        # any value works, so setting to TRUE
        anf_map[var] = ("any", None)

    elif line[0] == "ANF-var":
        try:
            var1 = int(line[1])
            var2 = int(line[4])
            inv = int(line[6])
        except ValueError:
            print("ERROR: Map file is corrupt")
            print("Tried parsing line:", line)
            exit(-1)

        assert line[5] == "^", "Map file is corrupt, we expected an '^'"
        assert var1 >= 0, "Map file is corrupt"
        assert var2 >= 0, "Map file is corrupt"
        assert inv == 0 or inv == 1, "Map file is corrupt, inversion is neither 0 nor 1"

        anf_map[var1] = ("prop", var2, inv)

    else:
        print("Map file corrupt, cannot understand line: ", line)

def parse_map(fname):
    anf_map = {}
    with open(fname, "r") as f:
        for line in f:
            parse_one_map_line(line, anf_map)

    return anf_map

if __name__ == "__main__":

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
        print("s ANF-UNSATISFIABLE")
        exit(0)
    # print ("CNF solution: ", cnfsol)

    anf_map = parse_map(mapfile)
    #print("ANF map:", anf_map)


    for also_any in [False, True]:
        something_done = True;
        while something_done:
            something_done = False;
            anf_map2 = dict(anf_map)
            for a,b in anf_map.items():
                if b[0] == "set":
                    continue

                if b[0] == "cnf":
                    var = b[1]
                    if var in cnfsol:
                        anf_map2[a] = ("set", cnfsol[var])
                        something_done = True

                if b[0] == "prop":
                    var = b[1]
                    inv = b[2]
                    if var in anf_map and anf_map[var][0] == "set":
                        anf_map2[a] = ("set", anf_map[var][1])
                        something_done = True

                if also_any and b[0] == "any":
                    anf_map2[a] = ("set", True)

                anf_map = dict(anf_map2)
                #print("ANF map:", anf_map)


    sol_in_txt = ""
    for a,b in anf_map.items():
        if b is None:
            continue

        assert b[0] == "set"

        extra = ""
        if b[1]:
            extra = "1+"
        sol_in_txt += " %sx(%d)" %(extra, a)

    print("c solution below, with variables starting at 0, as per ANF convention.")
    print("s ANF-SATISFIABLE")
    print("v%s" % sol_in_txt)
