#!/usr/bin/env python3
"""Check Bosphorus output against an independently computed ground truth.

Usage:
  verify_anf.py allsol     FILE.anf < bosphorus-allsol-output
  verify_anf.py solvewrite FILE.anf FILE.solution
  verify_anf.py cnf        FILE.anf FILE.cnf
"""

import itertools
import re
import sys

# A polynomial over GF(2) is a set of monomials; a monomial is a frozenset of
# variable indices, so the empty frozenset is the constant 1. Addition is
# symmetric difference, which gives x + x == 0 for free.
ONE = frozenset()


def poly_add(a, b):
    return a ^ b


def poly_mul(a, b):
    out = set()
    for ma in a:
        for mb in b:
            out ^= {ma | mb}
    return out


class Parser:
    """poly := term ('+' term)* ; term := factor ('*' factor)* ;
       factor := '(' poly ')' | 'x' int | 'x(' int ')' | '0' | '1'"""

    def __init__(self, text):
        self.s = text
        self.i = 0

    def ws(self):
        while self.i < len(self.s) and self.s[self.i] in ' \t':
            self.i += 1

    def peek(self):
        self.ws()
        return self.s[self.i] if self.i < len(self.s) else ''

    def expect(self, ch):
        if self.peek() != ch:
            raise ValueError("expected %r at offset %d in %r" % (ch, self.i, self.s))
        self.i += 1

    def parse(self):
        p = self.poly()
        if self.peek():
            raise ValueError("trailing input at offset %d in %r" % (self.i, self.s))
        return p

    def poly(self):
        acc = self.term()
        while self.peek() == '+':
            self.i += 1
            acc = poly_add(acc, self.term())
        return acc

    def term(self):
        acc = self.factor()
        while self.peek() == '*':
            self.i += 1
            acc = poly_mul(acc, self.factor())
        return acc

    def factor(self):
        c = self.peek()
        if c == '(':
            self.i += 1
            p = self.poly()
            self.expect(')')
            return p
        if c == 'x':
            self.i += 1
            if self.peek() == '(':
                self.i += 1
                n = self.number()
                self.expect(')')
            else:
                n = self.number()
            return {frozenset([n])}
        if c.isdigit():
            return {ONE} if self.number() % 2 else set()
        raise ValueError("unexpected %r at offset %d in %r" % (c, self.i, self.s))

    def number(self):
        self.ws()
        start = self.i
        while self.i < len(self.s) and self.s[self.i].isdigit():
            self.i += 1
        if start == self.i:
            raise ValueError("expected a number at offset %d in %r" % (self.i, self.s))
        return int(self.s[start:self.i])


def read_anf(path):
    polys = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('c'):
                continue
            polys.append(Parser(line).parse())
    return polys


def evaluate(poly, assign):
    val = 0
    for mono in poly:
        term = 1
        for v in mono:
            term &= assign[v]
        val ^= term
    return val


def all_vars(polys):
    seen = set()
    for p in polys:
        for mono in p:
            seen |= mono
    return seen


def brute_force(polys):
    """Every satisfying assignment over x0..x_max, matching Bosphorus's
    variable space (it reports every index up to the highest one used)."""
    used = all_vars(polys)
    n = (max(used) + 1) if used else 0
    if n > 20:
        sys.exit("verify_anf: %d variables is too many to brute force" % n)
    sols = set()
    for bits in itertools.product([0, 1], repeat=n):
        assign = dict(enumerate(bits))
        if all(evaluate(p, assign) == 0 for p in polys):
            sols.add(bits)
    return sols, n


def parse_solution_lines(text, nvars):
    """'v x(0) 1+x(1) ...' -- a bare x(i) is FALSE, 1+x(i) / x(i)+1 is TRUE."""
    sols = []
    for line in text.splitlines():
        if not line.startswith('v '):
            continue
        assign = {}
        for tok in line[2:].split():
            m = re.fullmatch(r'(1\+)?x\((\d+)\)(\+1)?', tok)
            if not m:
                sys.exit("verify_anf: cannot parse solution token %r" % tok)
            assign[int(m.group(2))] = 1 if (m.group(1) or m.group(3)) else 0
        if sorted(assign) != list(range(nvars)):
            sys.exit("verify_anf: solution covers vars %s, expected 0..%d"
                     % (sorted(assign), nvars - 1))
        sols.append(tuple(assign[i] for i in range(nvars)))
    return sols


def check_allsol(anf_path, output):
    polys = read_anf(anf_path)
    expected, nvars = brute_force(polys)
    reported = parse_solution_lines(output, nvars)

    if len(set(reported)) != len(reported):
        sys.exit("verify_anf: duplicate solutions reported")

    for sol in reported:
        assign = dict(enumerate(sol))
        for i, p in enumerate(polys):
            if evaluate(p, assign) != 0:
                sys.exit("verify_anf: reported solution %s does not satisfy equation %d"
                         % (sol, i))

    got = set(reported)
    if got != expected:
        sys.exit("verify_anf: solution set mismatch\n  missing: %s\n  spurious: %s"
                 % (sorted(expected - got), sorted(got - expected)))

    claimed = re.search(r'Number of solutions found:\s*(\d+)', output)
    if claimed and int(claimed.group(1)) != len(expected):
        sys.exit("verify_anf: reported count %s but there are %d solutions"
                 % (claimed.group(1), len(expected)))

    print("verify_anf: OK, %d solution(s) verified against brute force" % len(expected))


def check_solvewrite(anf_path, sol_path):
    """--solvewrite writes 'v -0 1 2 -3', where -N means x(N) is false."""
    polys = read_anf(anf_path)
    expected, nvars = brute_force(polys)
    text = open(sol_path).read()

    if 'SAT' not in text:
        sys.exit("verify_anf: no verdict in %s" % sol_path)
    if 'UNSAT' in text:
        if expected:
            sys.exit("verify_anf: reported UNSAT but there are %d solutions" % len(expected))
        print("verify_anf: OK, UNSAT confirmed by brute force")
        return

    assign = {}
    for line in text.splitlines():
        if not line.startswith('v '):
            continue
        for tok in line[2:].split():
            if not re.fullmatch(r'-?\d+', tok):
                sys.exit("verify_anf: cannot parse solution token %r" % tok)
            assign[int(tok.lstrip('-'))] = 0 if tok.startswith('-') else 1
    if not assign:
        sys.exit("verify_anf: no 'v' line in %s" % sol_path)
    if sorted(assign) != list(range(nvars)):
        sys.exit("verify_anf: solution covers vars %s, expected 0..%d"
                 % (sorted(assign), nvars - 1))

    for i, p in enumerate(polys):
        if evaluate(p, assign) != 0:
            sys.exit("verify_anf: solution does not satisfy equation %d" % i)
    if tuple(assign[i] for i in range(nvars)) not in expected:
        sys.exit("verify_anf: solution is not in the brute-forced solution set")

    print("verify_anf: OK, solution verified against brute force")


def read_cnf(path):
    clauses, xors, nvars, header = [], [], 0, None
    projection = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith('c p show'):
                projection = [int(t) for t in line[len('c p show'):].split() if t != '0']
                continue
            if line.startswith('c') or not line:
                continue
            if line.startswith('p '):
                header = int(line.split()[3])
                nvars = int(line.split()[2])
                continue
            xor = line.startswith('x')
            lits = [int(t) for t in (line[1:] if xor else line).split() if t != '0']
            (xors if xor else clauses).append(lits)
    return clauses, xors, nvars, header, projection


def check_cnf(anf_path, cnf_path):
    polys = read_anf(anf_path)
    expected, _ = brute_force(polys)
    clauses, xors, nvars, header, projection = read_cnf(cnf_path)

    if header != len(clauses) + len(xors):
        sys.exit("verify_anf: CNF header claims %d clauses, file has %d"
                 % (header, len(clauses) + len(xors)))
    for lits in clauses + xors:
        for lit in lits:
            if not 1 <= abs(lit) <= nvars:
                sys.exit("verify_anf: literal %d out of range 1..%d" % (lit, nvars))
    if projection is None:
        sys.exit("verify_anf: CNF has no 'c p show' projection line")

    if nvars > 20:
        sys.exit("verify_anf: %d CNF variables is too many to brute force" % nvars)

    models = set()
    for bits in itertools.product([0, 1], repeat=nvars):
        val = (0,) + bits
        if not all(any((lit > 0) == bool(val[abs(lit)]) for lit in c) for c in clauses):
            continue
        if not all(sum(val[abs(l)] ^ (l < 0) for l in x) % 2 == 1 for x in xors):
            continue
        models.add(tuple(val[v] for v in projection))

    if len(models) != len(expected):
        sys.exit("verify_anf: CNF has %d solutions over the projection set, "
                 "ANF has %d" % (len(models), len(expected)))

    print("verify_anf: OK, CNF well-formed with %d projected solution(s)" % len(models))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    mode = sys.argv[1]
    if mode == 'allsol':
        check_allsol(sys.argv[2], sys.stdin.read())
    elif mode == 'solvewrite':
        check_solvewrite(sys.argv[2], sys.argv[3])
    elif mode == 'cnf':
        check_cnf(sys.argv[2], sys.argv[3])
    else:
        sys.exit("verify_anf: unknown mode %r" % mode)


if __name__ == '__main__':
    main()
