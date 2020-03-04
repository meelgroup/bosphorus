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

if len(sys.argv) != 2:
    print("You must give a single file to validate")
    exit(-1)

cnffile = sys.argv[1]

max_var_used = 0
cls = 0

header_num_vars = None
header_num_cls = None
with open(cnffile, "r") as f:
    for line in f:
        line = line.strip()
        if line[0] == "c":
            continue

        if line == "":
            print("ERROR, empty line!")
            exit(-1)

        line = line.split()
        if line[0] == "p":
            assert len(line) == 4
            assert line[1] == "cnf"
            header_num_vars = int(line[2])
            header_num_cls = int(line[3])
            print("CNF: Given number of vars: %d, cls: %d" %
                  (header_num_vars, header_num_cls))
            continue


        cls += 1
        last_lit = None
        for l in line:
            v = abs(int(l))
            last_lit = int(l)
            max_var_used = max(v, max_var_used)

        if last_lit != 0:
            print("ERROR: Last literal of a clause must be 0")
            exit(-1)

if header_num_vars is None or header_num_cls is None:
    print("ERROR: No CNF header was found!")
    exit(-1)


if max_var_used > header_num_vars:
    print("ERROR:  Used variable %d but maximum variable declared was %d" %
          (max_var_used, header_num_vars))
    exit(-1)

if cls != header_num_cls:
    print("ERROR:  We had %d clauses, but header said %d" %
          (cls, header_num_cls))
    exit(-1)

print("CNF is well-formatted")
exit(0)
