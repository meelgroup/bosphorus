#!/usr/bin/env python3
"""Fuzzer for Bosphorus: random small ANF and CNF inputs, random options,
every answer checked against brute force.

  python3 utils/fuzz.py [--iters N] [--seed S] [--bin build/bosphorus]

ANF inputs are solved with --solve --allsol and the solution set is compared
with the brute-forced one (also for the ANF written by --anfwrite and, when
small enough, the CNF written by --cnfwrite). CNF inputs are read with
--cnfread and solved: the SAT/UNSAT answer must match brute force and a
reported model must satisfy the CNF. A failing input and its command line are
kept as fuzz-fail-<seed>.anf/.cnf for replay.
"""
import argparse, itertools, os, random, re, subprocess, sys, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tests', 'utils'))
from verify_anf import Parser, brute_force, parse_solution_lines, NAMES, NAME_RE, var_index, is_declaration  # noqa: E402


def _vname(style, v):
    if style < 0.4: return 'x(%d)' % v
    if style < 0.8: return 'x%d' % v
    return ['K[%d]', 'S_in[1,%d]', 'n_%d', 'v%da'][v % 4] % v  # named variables


def _mons_txt(mons, vname):
    return ' + '.join('1' if not m else '*'.join(vname(v) for v in sorted(m)) for m in mons)


def _random_polys(rng, nvars, neqs):
    """the original generator: random monomials, sometimes products of linerals"""
    out = []
    for _ in range(neqs):
        r = rng.random()
        if r < 0.25 and nvars >= 4:
            k = rng.randint(2, 3)
            vs = list(range(1, nvars + 1))
            rng.shuffle(vs)
            factors = []
            for i in range(k):
                sz = rng.randint(1, 3)
                if rng.random() < 0.3:  # shared variable
                    pick = rng.sample(vs, min(sz, len(vs)))
                else:
                    pick = [vs.pop() for _ in range(min(sz, len(vs)))] or [rng.randint(1, nvars)]
                factors.append((pick, rng.random() < 0.5))
            terms = {frozenset()}
            for pick, c in factors:
                nt = set()
                for t in terms:
                    if c:
                        nt ^= {t}
                    for v in pick:
                        nt ^= {t | {v}}
                terms = nt
            if not terms:
                continue
            out.append(sorted(terms, key=lambda t: sorted(t)))
        else:
            nmon = rng.randint(1, 6) if nvars <= 8 else rng.randint(4, 14)
            mons = []
            for _ in range(nmon):
                d = rng.choice([0, 1, 1, 2, 2, 3])
                mons.append(frozenset(rng.sample(range(1, nvars + 1), min(d, nvars))))
            out.append(mons)
    return out


def _circuit_polys(rng, nvars):
    """a random circuit written as Tseitin-style definitions y = gate(a, b):
    AND (a*b + y), OR (a*b + a + b + y), XOR (a + b + y), NAND, MAJ, plus a
    few output constraints and random extra relations between wires"""
    ninputs = rng.randint(2, 4)
    wires = list(range(1, ninputs + 1))
    polys = []
    while len(wires) < nvars:
        y = len(wires) + 1
        a, b = rng.sample(wires, 2) if len(wires) >= 2 else (wires[0], wires[0])
        gate = rng.choice(['and', 'or', 'xor', 'nand', 'maj', 'not'])
        if gate == 'and': mons = [{a, b}, {y}]
        elif gate == 'or': mons = [{a, b}, {a}, {b}, {y}]
        elif gate == 'xor': mons = [{a}, {b}, {y}]
        elif gate == 'nand': mons = [{a, b}, {y}, set()]
        elif gate == 'not': mons = [{a}, {y}, set()]
        else:
            c = rng.choice(wires)
            mons = [{a, b}, {a, c}, {b, c}, {y}]
        polys.append([frozenset(m) for m in mons])
        wires.append(y)
    # constrain some outputs / internal wires
    for _ in range(rng.randint(1, 3)):
        w = rng.choice(wires)
        polys.append([frozenset({w})] + ([frozenset()] if rng.random() < 0.5 else []))
    if rng.random() < 0.5:  # an extra relation between two wires
        a, b = rng.sample(wires, 2)
        polys.append([frozenset({a}), frozenset({b})] + ([frozenset()] if rng.random() < 0.5 else []))
    return polys


def _sbox_polys(rng, nvars):
    """a random small S-box (2-4 bits) given by its output ANFs, on random
    input/output wires, plus a few known bits: like the ascon instances"""
    n = rng.choice([2, 3, 3, 4]) if nvars >= 4 else 2
    n = min(n, nvars // 2)
    vs = list(range(1, nvars + 1))
    rng.shuffle(vs)
    ins, outs = vs[:n], vs[n:2 * n]
    # truth table -> ANF (Moebius transform) per output bit
    table = [rng.randrange(1 << n) for _ in range(1 << n)]
    polys = []
    for bit in range(n):
        f = [(table[x] >> bit) & 1 for x in range(1 << n)]
        # Moebius transform
        g = f[:]
        for i in range(n):
            for x in range(1 << n):
                if x & (1 << i): g[x] ^= g[x ^ (1 << i)]
        mons = [frozenset(ins[i] for i in range(n) if u & (1 << i)) for u in range(1 << n) if g[u]]
        mons.append(frozenset({outs[bit]}))
        polys.append(mons)
    for _ in range(rng.randint(0, n)):
        w = rng.choice(ins + outs)
        polys.append([frozenset({w})] + ([frozenset()] if rng.random() < 0.5 else []))
    if rng.random() < 0.5 and len(vs) > 2 * n:  # a linear layer into the leftovers
        for w in vs[2 * n:]:
            mons = [frozenset({v}) for v in rng.sample(ins + outs, min(2, 2 * n))] + [frozenset({w})]
            polys.append(mons)
    return polys


def _planted_polys(rng, nvars, neqs):
    """random polynomials adjusted to vanish on a planted assignment, so the
    system is satisfiable (dense, degree up to 4)"""
    sol = [rng.randrange(2) for _ in range(nvars + 1)]
    polys = []
    for _ in range(neqs):
        nmon = rng.randint(2, 10)
        mons = set()
        for _ in range(nmon):
            d = rng.choice([1, 2, 2, 3, 4])
            mons ^= {frozenset(rng.sample(range(1, nvars + 1), min(d, nvars)))}
        val = 0
        for m in mons:
            val ^= all(sol[v] for v in m)
        if val: mons ^= {frozenset()}
        if mons: polys.append(sorted(mons, key=lambda t: sorted(t)))
    return polys


def rand_anf(rng):
    # mostly small, sometimes up to 12 variables so that the linearisation
    # paths for polynomials with more than 10 variables are exercised
    nvars = rng.randint(2, 8) if rng.random() < 0.7 else rng.randint(9, 12)
    style = rng.random()
    vname = lambda v: _vname(style, v)
    kind = rng.choice(['random', 'random', 'circuit', 'sbox', 'planted', 'mixed'])
    if kind == 'random': polys = _random_polys(rng, nvars, rng.randint(1, 9))
    elif kind == 'circuit': polys = _circuit_polys(rng, nvars)
    elif kind == 'sbox': polys = _sbox_polys(rng, nvars)
    elif kind == 'planted': polys = _planted_polys(rng, nvars, rng.randint(1, 8))
    else:
        polys = _circuit_polys(rng, nvars)[:3] + _random_polys(rng, nvars, 3) + _sbox_polys(rng, nvars)[:2]
    rng.shuffle(polys)
    lines = [_mons_txt(m, vname) for m in polys if m]
    if not lines: lines = ['1 + ' + vname(1)]
    if rng.random() < 0.3:
        lines.insert(0, ', '.join(vname(v) for v in range(1, nvars + 1)))
    proj = None
    if rng.random() < 0.3:
        # a projection set: a random subset of the variables
        proj = sorted(rng.sample(range(1, nvars + 1), rng.randint(1, nvars)))
        lines.insert(0, 'c p show ' + ' '.join(vname(v) for v in proj) + ' END')
    return '\n'.join(lines) + '\n', nvars, proj


def verify_anf_index(v, txt):
    """ANF index of fuzzer variable v in the text (numbered: v itself, named: as verify_anf assigns it)"""
    m = re.search(r'(?m)^c p show (.*) END$', txt)
    style_named = 'x(' not in txt and re.search(r'\bx\d', txt) is None
    if not style_named: return v
    return var_index(_vname(0.9, v))


def rand_cnf(rng):
    nvars = rng.randint(2, 9)
    ncls = rng.randint(1, 14)
    cls = []
    for _ in range(ncls):
        k = rng.choice([1, 2, 2, 3, 3, 4, 5, 7])
        vs = rng.sample(range(1, nvars + 1), min(k, nvars))
        cls.append([v if rng.random() < 0.5 else -v for v in vs])
    txt = 'p cnf %d %d\n' % (nvars, len(cls))
    txt += ''.join(' '.join(map(str, c)) + ' 0\n' for c in cls)
    return txt, nvars, cls


def rand_opts(rng):
    o = []
    for flag in ['--xl', '--el', '--sat', '--rewrite', '--lingauss', '--spanfilter', '--binomred', '--shorten', '--probe',
                 '--faccanon', '--facres', '--gb', '--partner', '--factor', '--xorcls']:
        if rng.random() < 0.5:
            o += [flag, str(rng.randint(0, 1))]
    if rng.random() < 0.5: o += ['--cutnum', str(rng.randint(3, 6))]
    if rng.random() < 0.5: o += ['--karn', str(rng.randint(0, 10))]
    if rng.random() < 0.4: o += ['--karncluster', str(rng.randint(0, 12))]
    if rng.random() < 0.5: o += ['--projshow', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--xormaxlen', str(rng.randint(0, 5))]
    if rng.random() < 0.3: o += ['--maxiters', str(rng.randint(0, 4))]
    if rng.random() < 0.3: o += ['--probevars', str(rng.randint(2, 10))]
    if rng.random() < 0.3: o += ['--binomredlen', str(rng.randint(1, 4))]
    if rng.random() < 0.3: o += ['--keepfactor', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--xldeg', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--simplify', str(rng.randint(0, 1))]
    if rng.random() < 0.3: o += ['--gbdeg', str(rng.randint(1, 4))]
    if rng.random() < 0.3: o += ['--gbmaxvars', str(rng.randint(2, 12))]
    if rng.random() < 0.3: o += ['--gbfull', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--gbwholevars', str(rng.randint(0, 14))]
    if rng.random() < 0.3: o += ['--gbrecursion', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--gbengine', str(rng.randint(0, 1))]
    if rng.random() < 0.3: o += ['--gbtailreduce', str(rng.randint(0, 1))]
    if rng.random() < 0.3: o += ['--gbfactdeg', str(rng.randint(1, 3))]
    if rng.random() < 0.3: o += ['--gbfactlen', str(rng.randint(1, 12))]
    return o


def run(cmd, timeout=60):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, text=True)
    return p.returncode, p.stdout


def cnf_solutions(nvars, cls):
    sols = set()
    for bits in itertools.product([0, 1], repeat=nvars):
        ok = all(any((bits[abs(l) - 1] == 1) == (l > 0) for l in c) for c in cls)
        if ok: sols.add(bits)
    return sols


def check_anf(binary, rng, seed, tmpdir):
    txt, nvars, proj = rand_anf(rng)
    path = os.path.join(tmpdir, 'in.anf')
    open(path, 'w').write(txt)
    opts = rand_opts(rng)
    # 1) all solutions vs brute force
    cmd = [binary, '--anfread', path, '--solve', '--allsol', '--verb', '0'] + opts
    rc, out = run(cmd)
    from verify_anf import read_anf
    try:
        polys = read_anf(path)
        expected, nv = brute_force(polys)
    except SystemExit as e:
        return 'brute force: %s' % e, cmd, out
    if rc != 0:
        return 'exit %d' % rc, cmd, out
    # Bosphorus reports every variable up to the highest declared one, which
    # may exceed the highest one used in an equation (declaration line):
    # the extra variables are free, so compare projections and counts.
    reported = []
    for line in out.splitlines():
        if not line.startswith('v '): continue
        assign = {}
        for tok in line[2:].split():
            m = re.fullmatch(r'(1\+)?(x\(\d+\)|' + NAME_RE.pattern + r')(\+1)?', tok)
            if not m: return 'cannot parse solution token %r' % tok, cmd, out
            try:
                assign[var_index(m.group(2))] = 1 if (m.group(1) or m.group(3)) else 0
            except ValueError as e:
                return 'solution token: %s' % e, cmd, out
        if not assign or sorted(assign) != list(range(max(assign) + 1)):
            return 'solution covers vars %s' % sorted(assign), cmd, out
        reported.append(tuple(assign[i] for i in range(max(assign) + 1)))
    if len(set(reported)) != len(reported):
        return 'duplicate solutions', cmd, out
    extra = (len(reported[0]) - nv) if reported else 0
    if extra < 0:
        return 'solution covers fewer variables than the equations use', cmd, out
    if proj is not None:
        # --allsol enumerates one solution per assignment of the projected
        # variables: each reported one must be a solution, and projected
        # onto the projection set they must be exactly the projected
        # brute-forced solutions
        pidx = [verify_anf_index(v, txt) for v in proj]
        got_full = set(tuple(s[:nv]) for s in reported)
        if not got_full <= expected:
            return 'a reported solution is not a solution', cmd, out
        # a projected variable used in no equation is free: extend the
        # brute-forced solutions over it
        n_all = max([nv] + [i + 1 for i in pidx])
        if n_all > nv:
            ext = set()
            for s0 in expected:
                for bits in itertools.product([0, 1], repeat=n_all - nv):
                    ext.add(tuple(s0) + bits)
            expected = ext
            if reported and len(reported[0]) < n_all:
                return 'solution covers fewer variables than the projection', cmd, out
        exp_proj = set(tuple(s[i] for i in pidx) for s in expected)
        got_proj = [tuple(s[i] for i in pidx) for s in reported]
        if set(got_proj) != exp_proj or len(got_proj) != len(exp_proj):
            return 'projected solutions differ: expected %d got %d' % (len(exp_proj), len(got_proj)), cmd, out
    else:
        got = set(tuple(s[:nv]) for s in reported)
        if got != expected or len(reported) != len(expected) * (1 << extra):
            return 'solutions differ: expected %d got %d (extra free vars %d)' % (len(expected), len(reported), extra), cmd, out
    # 2) the written ANF must have the same solutions (it lists fixed values and equivalences)
    outanf = os.path.join(tmpdir, 'out.anf')
    cmd2 = [binary, '--anfread', path, '--anfwrite', outanf, '--verb', '0'] + opts
    rc, out2 = run(cmd2)
    if rc != 0:
        return 'anfwrite exit %d' % rc, cmd2, out2
    # the written ANF uses the same names (x(N) for numbered variables);
    # keep the name table of the input while parsing it
    saved = dict(NAMES)
    polys2 = []
    for l in open(outanf).read().splitlines():
        if not l or l.startswith('c') or (',' in l and is_declaration(l)): continue
        polys2.append(Parser(l).parse())
    NAMES.clear(); NAMES.update(saved)
    exp2, _ = brute_force(polys2)
    # brute_force() derives the variable count from the polys: compare on the common variables
    if exp2 != expected:
        # the written ANF may use fewer variables (dropped ones are free): compare projections
        def proj(sols, n):
            return set(tuple(s[:n]) for s in sols)
        n = min(len(next(iter(exp2))) if exp2 else 0, len(next(iter(expected))) if expected else 0)
        if not (exp2 and expected) or proj(exp2, n) != proj(expected, n):
            return 'written ANF has different solutions', cmd2, out2
    # 3) the written CNF must have the same solutions when small enough to brute force
    outcnf = os.path.join(tmpdir, 'out.cnf')
    cmd3 = [binary, '--anfread', path, '--cnfwrite', outcnf, '--verb', '0'] + opts
    rc, out3 = run(cmd3)
    if rc != 0:
        return 'cnfwrite exit %d' % rc, cmd3, out3
    header = re.search(r'^p cnf (\d+)', open(outcnf).read(), re.M)
    if header and int(header.group(1)) <= 16 and '--xorcls' not in opts:
        verify = os.path.join(HERE, '..', 'tests', 'utils', 'verify_anf.py')
        rc, out4 = run([sys.executable, verify, 'cnf', path, outcnf])
        if rc != 0:
            return 'written CNF differs: %s' % out4.strip().splitlines()[-1], cmd3, out4
    return None, cmd, out


def check_cnf(binary, rng, seed, tmpdir):
    txt, nvars, cls = rand_cnf(rng)
    path = os.path.join(tmpdir, 'in.cnf')
    open(path, 'w').write(txt)
    opts = rand_opts(rng)
    cmd = [binary, '--cnfread', path, '--solve', '--allsol', '--verb', '0'] + opts
    rc, out = run(cmd)
    if rc != 0:
        return 'exit %d' % rc, cmd, out
    sols = cnf_solutions(nvars, cls)
    sat = 's ANF-SATISFIABLE' in out
    unsat = 's ANF-UNSATISFIABLE' in out
    if not sat and not unsat:
        return 'no answer', cmd, out
    if sat != bool(sols):
        return 'wrong answer: brute force says %s' % ('SAT' if sols else 'UNSAT'), cmd, out
    # every model "v x(0) 1+x(1) ..." (ANF variable x(i) = CNF variable i+1)
    # must satisfy the CNF, and projected onto the CNF's variables the
    # models must be exactly the brute-forced solutions
    got = set()
    for m in re.finditer(r'^v (.*)$', out, re.M):
        assign = {}
        for tok in m.group(1).split():
            true = tok.startswith('1+')  # "1+x(i)" is true, a bare "x(i)" is false
            v = int(re.search(r'x\((\d+)\)', tok).group(1)) + 1
            if v <= nvars: assign[v] = true
        for c in cls:
            if not any(assign.get(abs(l), False) == (l > 0) for l in c):
                return 'model does not satisfy the CNF', cmd, out
        got.add(tuple(1 if assign.get(v, False) else 0 for v in range(1, nvars + 1)))
    if got != sols:
        return 'solution set differs: expected %d got %d' % (len(sols), len(got)), cmd, out
    return None, cmd, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--iters', type=int, default=60)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--bin', default=os.path.join(HERE, '..', 'build', 'bosphorus'))
    a = ap.parse_args()
    base_seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    tmpdir = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'bosph-fuzz-%d' % os.getpid())
    os.makedirs(tmpdir, exist_ok=True)
    fails = 0
    for i in range(a.iters):
        seed = base_seed + i
        rng = random.Random(seed)
        kind = 'cnf' if rng.random() < 0.35 else 'anf'
        try:
            err, cmd, out = (check_cnf if kind == 'cnf' else check_anf)(a.bin, rng, seed, tmpdir)
        except subprocess.TimeoutExpired:
            err, cmd, out = 'timeout', ['(timeout)'], ''
        if err:
            fails += 1
            keep = 'fuzz-fail-%d.%s' % (seed, kind)
            shutil.copy(os.path.join(tmpdir, 'in.' + kind), keep)
            print('FAIL seed %d (%s): %s\n  input: %s\n  cmd: %s' % (seed, kind, err, keep, ' '.join(cmd)))
            print('  output tail: ' + ' | '.join(out.strip().splitlines()[-3:]))
    shutil.rmtree(tmpdir, ignore_errors=True)
    print('fuzz: %d iterations, base seed %d, %d failure(s)' % (a.iters, base_seed, fails))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
