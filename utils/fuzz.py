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
from verify_anf import Parser, brute_force, parse_solution_lines  # noqa: E402


def rand_anf(rng):
    # mostly small, sometimes up to 12 variables so that the linearisation
    # paths for polynomials with more than 10 variables are exercised
    nvars = rng.randint(2, 8) if rng.random() < 0.7 else rng.randint(9, 12)
    neqs = rng.randint(1, 9)
    lines = []
    style = rng.random()
    for _ in range(neqs):
        r = rng.random()
        if r < 0.25 and nvars >= 4:
            # a product of linear factors, written out (exercises the factor code)
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
            mons = sorted(terms, key=lambda t: sorted(t))
        else:
            nmon = rng.randint(1, 6) if nvars <= 8 else rng.randint(4, 14)
            mons = []
            for _ in range(nmon):
                d = rng.choice([0, 1, 1, 2, 2, 3])
                mons.append(frozenset(rng.sample(range(1, nvars + 1), min(d, nvars))))
        txt = ' + '.join('1' if not m else '*'.join(('x(%d)' % v if style < 0.5 else 'x%d' % v)
                                                 for v in sorted(m)) for m in mons)
        lines.append(txt)
    if rng.random() < 0.3:
        lines.insert(0, ', '.join('x%d' % v for v in range(1, nvars + 1)))
    return '\n'.join(lines) + '\n', nvars


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
    for flag in ['--xl', '--el', '--sat', '--rewrite', '--binomred', '--shorten', '--probe',
                 '--faccanon', '--facres', '--gb', '--partner', '--factor', '--xorcls']:
        if rng.random() < 0.5:
            o += [flag, str(rng.randint(0, 1))]
    if rng.random() < 0.5: o += ['--cutnum', str(rng.randint(3, 6))]
    if rng.random() < 0.5: o += ['--karn', str(rng.randint(0, 10))]
    if rng.random() < 0.3: o += ['--xormaxlen', str(rng.randint(0, 5))]
    if rng.random() < 0.3: o += ['--maxiters', str(rng.randint(0, 4))]
    if rng.random() < 0.3: o += ['--probevars', str(rng.randint(2, 10))]
    if rng.random() < 0.3: o += ['--binomredlen', str(rng.randint(1, 4))]
    if rng.random() < 0.3: o += ['--keepfactor', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--xldeg', str(rng.randint(0, 2))]
    if rng.random() < 0.3: o += ['--simplify', str(rng.randint(0, 1))]
    if rng.random() < 0.3: o += ['--gbdeg', str(rng.randint(1, 4))]
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
    txt, nvars = rand_anf(rng)
    path = os.path.join(tmpdir, 'in.anf')
    open(path, 'w').write(txt)
    opts = rand_opts(rng)
    # 1) all solutions vs brute force
    cmd = [binary, '--anfread', path, '--solve', '--allsol', '--verb', '0'] + opts
    rc, out = run(cmd)
    polys = [Parser(l).parse() for l in txt.splitlines() if l and not l.startswith('c') and ',' not in l]
    try:
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
            m = re.fullmatch(r'(1\+)?x\((\d+)\)(\+1)?', tok)
            if not m: return 'cannot parse solution token %r' % tok, cmd, out
            assign[int(m.group(2))] = 1 if (m.group(1) or m.group(3)) else 0
        if not assign or sorted(assign) != list(range(max(assign) + 1)):
            return 'solution covers vars %s' % sorted(assign), cmd, out
        reported.append(tuple(assign[i] for i in range(max(assign) + 1)))
    if len(set(reported)) != len(reported):
        return 'duplicate solutions', cmd, out
    extra = (len(reported[0]) - nv) if reported else 0
    if extra < 0:
        return 'solution covers fewer variables than the equations use', cmd, out
    got = set(tuple(s[:nv]) for s in reported)
    if got != expected or len(reported) != len(expected) * (1 << extra):
        return 'solutions differ: expected %d got %d (extra free vars %d)' % (len(expected), len(reported), extra), cmd, out
    # 2) the written ANF must have the same solutions (it lists fixed values and equivalences)
    outanf = os.path.join(tmpdir, 'out.anf')
    cmd2 = [binary, '--anfread', path, '--anfwrite', outanf, '--verb', '0'] + opts
    rc, out2 = run(cmd2)
    if rc != 0:
        return 'anfwrite exit %d' % rc, cmd2, out2
    lines = [l for l in open(outanf).read().splitlines() if l and not l.startswith('c') and ',' not in l]
    polys2 = [Parser(l).parse() for l in lines]
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
    cmd = [binary, '--cnfread', path, '--solve', '--verb', '0'] + opts
    rc, out = run(cmd)
    if rc != 0:
        return 'exit %d' % rc, cmd, out
    sols = cnf_solutions(nvars, cls)
    sat = 's ANF-SATISFIABLE' in out
    unsat = 's ANF-UNSATISFIABLE' in out
    if sat == unsat:
        return 'no clear answer', cmd, out
    if sat != bool(sols):
        return 'wrong answer: brute force says %s' % ('SAT' if sols else 'UNSAT'), cmd, out
    if sat:
        # the model: "v x(0) 1+x(1) ..." over ANF variables x(i) = CNF variable i+1
        m = re.search(r'^v (.*)$', out, re.M)
        assign = {}
        for tok in m.group(1).split():
            true = tok.startswith('1+')  # "1+x(i)" is true, a bare "x(i)" is false
            v = int(re.search(r'x\((\d+)\)', tok).group(1)) + 1
            if v <= nvars: assign[v] = true
        for c in cls:
            if not any(assign.get(abs(l), False) == (l > 0) for l in c):
                return 'model does not satisfy the CNF', cmd, out
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
