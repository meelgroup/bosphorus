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

import sys
import check_solution

def get_max_var(anf):
    maxvar = -1
    for line in anf:
        line = line.strip()

        # empty lines in ANF are OK
        if line == "":
            continue

        # ignore comments
        if line[0] == "c":
            continue

        monoms = line.split("+")
        for m in monoms:
            m = m.strip()
            if m == "1":
                continue
            vars = m.split("*")
            for v in vars:
                v = v.replace("x", "").replace("(", "").replace(")", "").strip()
                if int(v) > maxvar:
                    maxvar= int(v)

    return maxvar


def count_sols(anf, maxvar):
    if maxvar == -1:
        return 0

    num_sols = 0
    for solution in range(2**maxvar):
        sol = {}
        for i in range(maxvar):
            sol[i] = (solution>>i)&1

        ok, eq, monoms = check_solution.check_anf_against_solution(anf, sol)
        if ok:
            num_sols += 1

    return num_sols


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Must give ANF file")
        exit(-1)

    anffile = sys.argv[1]
    with open(anffile, "r") as f:
        anf = f.read().split("\n")

    maxvar = get_max_var(anf)+1
    if maxvar > 10:
        print("WARN: too many variables, not checking solutions count")
        exit(0)

    num_sols = count_sols(anf, maxvar)
    print("Num solutions to ANF: %d" % num_sols)


    if len(sys.argv) < 3:
        exit(0)


    solver_output = sys.argv[2]
    maxsol_cnf = 0
    with open(solver_output, "r") as f:
        for line in f:
            if "Number of solutions" in line:
                line = line.split()
                num_sol = line[7].strip()
                sol = int(num_sol)
                maxsol_cnf = max(sol, maxsol_cnf)

    print("Num solutions to CNF: %d" % maxsol_cnf)

    if maxsol_cnf != num_sols:
        print("ERROR: the number of solutions differ!")
        exit(-1)
    else:
        print("All good, solution counts match.")
    exit(0)


