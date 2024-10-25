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
import random

def get_monomial():
    deg = int(random.gammavariate(0.2, 4))+1
    assert deg >0

    already_in = []
    ret = ""
    for x in range(deg):
        if x != 0:
            ret += "*"
        ret += "x(%d)" % random.randint(0, max_var)

    return ret


def gen_equation():
    if random.choice([True, True, False]):
        num_monoms = int(random.gammavariate(1, 30))+1
    else:
        num_monoms = int(random.gammavariate(1, 3))+1
    assert num_monoms >0

    ret = ""
    for x in range(num_monoms):
        if x != 0:
            ret += "+"

        ret += get_monomial()

    if random.choice([True, False]):
        ret += " + 1"

    return ret


if len(sys.argv) > 2:
    print("Usage: anf_gen.py [seed]")


if len(sys.argv) == 2:
    try:
        seed = sys.argv[1]
    except ValueError:
        print("ERROR: seed must be an integer")
        exit(-1)


random.seed(seed)
if random.randint(1,3) == 0:
    # for solution counting
    max_var = random.randint(2, 10)
else:
    max_var = random.randint(5, 50)

num_eqs = random.randint(int(max_var/4), max_var*3)
for x in range(num_eqs):
    eq = gen_equation()
    print(eq)

