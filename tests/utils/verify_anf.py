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


NAME_RE = re.compile(r'[A-Za-z_][A-Za-z0-9_]*(?:\[[^\]]*\])?')
NAMES = {}  # named variable -> index, as Bosphorus assigns them (see scan_names)


def var_index(name):
    """x<N> and x(N) have index N; other names are looked up in NAMES."""
    m = re.fullmatch(r'[xX]\(?(\d+)\)?', name)
    if m:
        return int(m.group(1))
    if name not in NAMES:
        raise ValueError("unknown variable %r" % name)
    return NAMES[name]


def scan_names(path):
    """Assign indices to named variables like Bosphorus: after the highest
    x<N> index, in order of first appearance (declaration lines included)."""
    NAMES.clear()
    max_num, order = -1, []
    with open(path) as f:
        for line in f:
            if line.startswith('c'):
                continue
            for m in re.finditer(r'[xX]\((\d+)\)', line):
                max_num = max(max_num, int(m.group(1)))
            for tok in NAME_RE.findall(line):
                m = re.fullmatch(r'[xX](\d+)', tok)
                if m:
                    max_num = max(max_num, int(m.group(1)))
                    continue
                if tok in ('x', 'X'):
                    continue
                if tok not in order:
                    order.append(tok)
    for i, name in enumerate(order):
        NAMES[name] = max_num + 1 + i


VAR_RE = r'(?:[xX]\(\d+\)|' + NAME_RE.pattern + r')'
DECL_RE = re.compile(r'\s*' + VAR_RE + r'(?:\s*,\s*' + VAR_RE + r')+\s*')


def is_declaration(line):
    """'v1, v2, v3': a comma separated list of variables (names may contain
    commas inside brackets, so this is matched as a whole)."""
    return DECL_RE.fullmatch(line.strip()) is not None


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
        m = NAME_RE.match(self.s, self.i)
        if m:
            self.i = m.end()
            return {frozenset([var_index(m.group(0))])}
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
    scan_names(path)
    polys = []
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('c'):
                continue
            if ',' in line and is_declaration(line):
                continue  # a variable declaration line, not an equation
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
            m = re.fullmatch(r'(1\+)?(x\(\d+\)|' + NAME_RE.pattern + r')(\+1)?', tok)
            if not m:
                sys.exit("verify_anf: cannot parse solution token %r" % tok)
            assign[var_index(m.group(2))] = 1 if (m.group(1) or m.group(3)) else 0
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


def max_var_in_file(path):
    """Highest variable index mentioned anywhere in the file (the ring
    Bosphorus builds covers every mentioned variable, even ones that cancel
    out of every equation or only appear in a declaration line)."""
    m = -1
    with open(path) as f:
        for line in f:
            if line.startswith('c'):
                continue
            for v in re.findall(r'x\(?(\d+)', line):
                m = max(m, int(v))
    scan_names(path)
    for idx in NAMES.values():
        m = max(m, idx)
    return m


def check_cnf(anf_path, cnf_path):
    polys = read_anf(anf_path)
    clauses, xors, nvars, header, projection = read_cnf(cnf_path)

    if header != len(clauses) + len(xors):
        sys.exit("verify_anf: CNF header claims %d clauses, file has %d"
                 % (header, len(clauses) + len(xors)))
    for lits in clauses + xors:
        for lit in lits:
            if not 1 <= abs(lit) <= nvars:
                sys.exit("verify_anf: literal %d out of range 1..%d" % (lit, nvars))
    if projection is None:
        # no 'c p show' line (the default): CNF variables 1..ring size are
        # the ANF's variables, everything after them is auxiliary
        ring = max(max_var_in_file(anf_path) + 1, 1)
        if nvars < ring:
            sys.exit("verify_anf: CNF has %d variables, the ANF ring %d" % (nvars, ring))
        projection = list(range(1, ring + 1))

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

    # the ANF's solutions over the whole ring, projected onto the same
    # variables (CNF variable j is ANF variable x(j-1))
    # Bosphorus's ring covers every mentioned variable and always x(0)
    proj_anf = [v - 1 for v in projection]
    n = max([max_var_in_file(anf_path) + 1, 1] + [v + 1 for v in proj_anf])
    if n > 20:
        sys.exit("verify_anf: %d variables is too many to brute force" % n)
    expected = set()
    for bits in itertools.product([0, 1], repeat=n):
        assign = dict(enumerate(bits))
        if all(evaluate(p, assign) == 0 for p in polys):
            expected.add(tuple(bits[v] for v in proj_anf))

    if models != expected:
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
