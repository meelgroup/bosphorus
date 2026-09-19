#!/usr/bin/env python3
"""Fuzzer for Bosphorus: random small ANF and CNF inputs, random options,
every count checked with ganak (never by enumeration).

  python3 utils/fuzz.py [--iters N (default 0: endless)] [--seed S] [--bin build/bosphorus] [--ganak PATH]

ANF inputs: the --allsol solutions satisfy the equations and their number is
ganak's (projected) count; the ANF written by --anfwrite, the CNF written by
--cnfwrite and the CNF of the written ANF have the same projected count.
CNF inputs ('c p show' / 'c ind' projection lines included): the same for
--allsol, --cnfwrite, --anfwrite and --anfwrite followed by --cnfwrite.
A run over 5 s is skipped, not a failure. Every file is created with
unique_file() in out/ (several fuzzers can share the directory, see
fuzz_session.sh) and deleted after the iteration unless it failed: then the
files are kept with a repro_N.sh and the fuzzer exits, printing how to
re-run and re-generate the failure.
"""
import argparse, collections, itertools, os, random, re, shlex, shutil, signal, stat, subprocess, sys, time, traceback

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'tests', 'utils'))
from verify_anf import Parser, NAMES, NAME_RE, var_index, is_declaration, read_anf, read_cnf, evaluate, max_var_in_file  # noqa: E402


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


def _expand_product(factors):
    terms = {frozenset()}
    for pick, c in factors:
        nt = set()
        for t in terms:
            if c: nt ^= {t}
            for v in pick: nt ^= {t | {v}}
        terms = nt
    return sorted(terms, key=lambda t: sorted(t))


def _subst_product_polys(rng, nvars):
    """short products of linerals (x*y, x*y + x, x*y + x*z: stored as
    polynomials, not as products) and 4+-term ones, then units and
    equivalences over their variables so that propagation substitutes into
    them: factors merge, become equal or constant, and two equations become
    duplicates"""
    polys = []
    prods = []
    for _ in range(rng.randint(1, 5)):
        factors = []
        for _ in range(rng.randint(2, 3)):
            pick = rng.sample(range(1, nvars + 1), min(rng.choice([1, 1, 1, 2, 2, 3]), nvars))
            factors.append((pick, rng.random() < 0.4))
        prods.append(factors)
        mons = _expand_product(factors)
        if mons: polys.append(mons)
    if prods and rng.random() < 0.5:
        # the same product again with one variable renamed to an equivalent one
        factors = rng.choice(prods)
        a = rng.choice(factors[0][0])
        b = rng.randint(1, nvars)
        renamed = [([b if v == a else v for v in pick], c) for pick, c in factors]
        renamed = [(sorted(set(pick)), c) for pick, c in renamed]
        mons = _expand_product(renamed)
        if mons: polys.append(mons)
        if a != b: polys.append([frozenset({a}), frozenset({b})])
    for _ in range(rng.randint(1, 4)):
        a, b = rng.sample(range(1, nvars + 1), 2) if nvars >= 2 else (1, 1)
        r = rng.random()
        if r < 0.5: polys.append([frozenset({a}), frozenset({b})] + ([frozenset()] if rng.random() < 0.5 else []))
        elif r < 0.8: polys.append([frozenset({a})] + ([frozenset()] if rng.random() < 0.5 else []))
        else:
            c = rng.randint(1, nvars)
            polys.append([frozenset({a}), frozenset({b}), frozenset({c})])
    return [p for p in polys if p]


def rand_anf(rng):
    # mostly small, sometimes up to 12 variables so that the linearisation
    # paths for polynomials with more than 10 variables are exercised
    nvars = rng.randint(2, 8) if rng.random() < 0.7 else rng.randint(9, 12)
    style = rng.random()
    vname = lambda v: _vname(style, v)
    kind = rng.choice(['random', 'random', 'circuit', 'sbox', 'planted', 'subst', 'subst', 'mixed'])
    if kind == 'random': polys = _random_polys(rng, nvars, rng.randint(1, 9))
    elif kind == 'circuit': polys = _circuit_polys(rng, nvars)
    elif kind == 'sbox': polys = _sbox_polys(rng, nvars)
    elif kind == 'planted': polys = _planted_polys(rng, nvars, rng.randint(1, 8))
    elif kind == 'subst': polys = _subst_product_polys(rng, nvars)
    else:
        polys = _circuit_polys(rng, nvars)[:3] + _random_polys(rng, nvars, 3) + _sbox_polys(rng, nvars)[:2] + _subst_product_polys(rng, nvars)[:3]
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
    proj = None
    if rng.random() < 0.4:
        # a projection set, possibly split over several lines, in either syntax
        proj = sorted(rng.sample(range(1, nvars + 1), rng.randint(1, nvars)))
        rest, lines = list(proj), []
        while rest:
            k = rng.randint(1, len(rest))
            lines.append(rng.choice(['c p show', 'c ind']) + ''.join(' %d' % v for v in rest[:k]) + ' 0\n')
            rest = rest[k:]
        txt = ''.join(lines) + txt if rng.random() < 0.5 else txt + ''.join(lines)
    txt += ''.join(' '.join(map(str, c)) + ' 0\n' for c in cls)
    return txt, nvars, cls, proj


# Every option is listed by hand (never derived from --help): a new rule or
# cutoff must be added here. All are always passed: switches with a random
# value in 0..max, cutoffs with the extremes or the default.
SWITCHES = [('--simplify', 1), ('--rewrite', 1), ('--lingauss', 1), ('--spanfilter', 1), ('--binomred', 1),
            ('--shorten', 1), ('--monogauss', 1), ('--prodsplit', 1), ('--probe', 1), ('--varprobe', 1), ('--cnfprobe', 1), ('--cnfprobebin', 2),
            ('--faccanon', 1), ('--facres', 1), ('--gb', 2), ('--gbfull', 2), ('--gbengine', 2),
            ('--gbrecursion', 2), ('--gbtailreduce', 1), ('--keepfactor', 2), ('--xl', 1), ('--el', 1),
            ('--sat', 1), ('--factor', 1), ('--partner', 1), ('--xorcls', 1), ('--projshow', 2)]
EXTREME = [0, 1, 2, 3, 10, 100000, 10000000]
CUTOFFS = [  # (flag, default, choices other than the extremes; None: the extremes)
    ('--cutnum', 5, [3, 4, 6, 10]),
    ('--karn', 10, [0, 1, 2, 3, 20]),
    ('--xldeg', 1, [0, 2, 3]),
    ('--xlsample', 30, [0, 1, 2, 10, 40]),
    ('--xlsamplex', 4, [0, 1, 10]),
    ('--elsample', 30, [0, 1, 2, 10, 40]),
    ('--karncluster', 10, None), ('--quadsplit', 0, None), ('--quadsplitmin', 2, None), ('--xormaxlen', 0, None), ('--maxiters', 100, None),
    ('--rewriterounds', 10, None), ('--binomredlen', 2, None), ('--monogausslen', 64, None),
    ('--monogausscols', 100000, None), ('--varprobebudget', 2000000, None), ('--varprobelen', 64, None),
    ('--probevars', 8, None), ('--facresmax', 2, None), ('--gbdeg', 3, None), ('--gbwindow', 24, None),
    ('--gbmaxvars', 16, None), ('--gbmaxcells', 2000000000, [50, 500, 5000]), ('--gbsplit', 8, None),
    ('--cnfprobevars', 200000, None), ('--cnfprobelen', 8, None), ('--gbsplitrows', 20000000, None), ('--gbf5groups', 8, None), ('--gbwholevars', 40, None),
    ('--gbmaxlen', 32, None), ('--gbsteps', 100000, None), ('--gbfactdeg', 2, None), ('--gbconefactdeg', 1, None), ('--gbfactlen', 8, None),
    ('--satinc', 10000, None), ('--satlim', 100000, None),
]


def rand_opts(rng):
    o = []
    for flag, mx in SWITCHES:
        o += [flag, str(rng.randint(0, mx))]
    for flag, default, choices in CUTOFFS:
        v = default if rng.random() < 0.3 else rng.choice(EXTREME if choices is None else choices)
        o += [flag, str(v)]
    return o


class Timeout(Exception):
    pass


def run(cmd, timeout=5):
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout, text=True)
    except subprocess.TimeoutExpired:
        raise Timeout()
    return p.returncode, p.stdout


GANAK = None
FILES = []  # the files of the current iteration: deleted unless it failed


def unique_file(prefix, suffix, max_num_files=10000):
    """a new file in out/, created atomically so that fuzzers running in
    parallel in the same directory never share one"""
    counter = 1
    while True:
        path = "out/" + prefix + '_' + str(counter) + suffix
        try:
            fd = os.open(path, os.O_CREAT | os.O_EXCL, stat.S_IREAD | stat.S_IWRITE)
            os.fdopen(fd).close()
            FILES.append(path)
            return path
        except OSError:
            pass
        counter += 1
        if counter > max_num_files:
            print(f"Cannot create unique_file, last try was: {path}")
            sys.exit(-1)
STATS = collections.Counter()  # completed checks, printed at the end so that a dead check shows


def xor_clauses(vs, rhs, fresh):
    """XOR(vs) = rhs as clauses, cut into pieces of at most 4 variables"""
    out = []
    while len(vs) > 4:
        t = fresh()
        out += xor_clauses(vs[:3] + [t], 0, fresh)
        vs = [t] + vs[3:]
    if not vs:
        if rhs:
            t = fresh()
            out += [[t], [-t]]
        return out
    for signs in itertools.product([1, -1], repeat=len(vs)):
        # forbid every assignment of the wrong parity (sign -1: variable true)
        if signs.count(-1) % 2 != rhs:
            out.append([s * v for s, v in zip(signs, vs)])
    return out


def anf_cnf(polys, base):
    """Tseitin encoding, ANF variable i is CNF variable i+1; the auxiliary
    variables (above base) are functions of the ANF variables"""
    nv = [max([base] + [v + 1 for p in polys for m in p for v in m])]
    def fresh():
        nv[0] += 1
        return nv[0]
    cls, ands = [], {}
    for p in polys:
        lits, rhs = [], 0
        for m in p:
            if not m: rhs ^= 1
            elif len(m) == 1: lits.append(next(iter(m)) + 1)
            else:
                if m not in ands:
                    t = ands[m] = fresh()
                    cls += [[-t, v + 1] for v in m] + [[t] + [-(v + 1) for v in m]]
                lits.append(ands[m])
        cls += xor_clauses(lits, rhs, fresh)
    return cls, nv[0]


def cnf_with_xors(cls, xors, nvars):
    """a written CNF's XOR lines 'x1 -2 3 0' (odd parity, a negation flips it) as clauses"""
    nv = [nvars]
    def fresh():
        nv[0] += 1
        return nv[0]
    out = list(cls)
    for x in xors:
        out += xor_clauses([abs(l) for l in x], 1 ^ (sum(l < 0 for l in x) % 2), fresh)
    return out, nv[0]


def count(cls, nvars, proj):
    """ganak's model count projected onto proj (CNF variables)"""
    # an extra free projected variable: the projection set is never empty
    extra = max([nvars] + list(proj)) + 1
    path = unique_file('count', '.cnf')
    with open(path, 'w') as f:
        f.write('c t pmc\np cnf %d %d\n' % (extra, len(cls)))
        f.write('c p show ' + ' '.join(map(str, sorted(set(proj)) + [extra])) + ' 0\n')
        f.writelines(' '.join(map(str, c)) + ' 0\n' for c in cls)
    _, out = run([GANAK, path], timeout=20)
    m = re.search(r'^c s exact arb int (\d+)$', out, re.M)
    if not m:
        raise RuntimeError('ganak gave no count on %s:\n%s' % (path, out[-2000:]))
    return int(m.group(1)) // 2


def written_cnf_count(path):
    ocls, oxors, onv, _, oproj = read_cnf(path)
    if oproj is None:
        return None
    cls, nv = cnf_with_xors(ocls, oxors, onv)
    return count(cls, nv, oproj)


def with_projshow(opts, v):
    o = list(opts)
    o[o.index('--projshow') + 1] = str(v)
    return o


def read_written_anf(path):
    """the polynomials of an ANF Bosphorus wrote, with the variable names of its input"""
    saved = dict(NAMES)
    polys = []
    for l in open(path).read().splitlines():
        if not l or l.startswith('c') or (',' in l and is_declaration(l)): continue
        polys.append(Parser(l).parse())
    NAMES.clear(); NAMES.update(saved)
    return polys


def anf_count_via_cnf(binary, anf_path, opts, projshow, expect, what):
    """ANF -> --cnfwrite -> the count over its 'c p show' line must be expect"""
    outcnf = unique_file('written', '.cnf')
    cmd = [binary, '--anfread', anf_path, '--cnfwrite', outcnf, '--verb', '0'] + with_projshow(opts, projshow)
    rc, out = run(cmd)
    if rc != 0:
        return '%s: exit %d' % (what, rc), cmd, out
    c = written_cnf_count(outcnf)
    if c is None:
        return '%s: no projection line' % what, cmd, out
    if c != expect:
        return '%s: %d projected models, expected %d' % (what, c, expect), cmd, out
    STATS[what] += 1
    return None


def check_anf(binary, rng):
    txt, nvars, proj = rand_anf(rng)
    path = unique_file('input', '.anf')
    open(path, 'w').write(txt)
    opts = rand_opts(rng)
    polys = read_anf(path)
    ring = max_var_in_file(path) + 1
    pidx = [verify_anf_index(v, txt) for v in proj] if proj is not None else None
    if pidx: ring = max([ring] + [i + 1 for i in pidx])

    # 1) --allsol: every reported solution satisfies the equations, and there
    # is exactly one per solution (per projected solution with a projection
    # set), so their number is ganak's (projected) count
    cmd = [binary, '--anfread', path, '--solve', '--allsol', '--verb', '0'] + opts
    rc, out = run(cmd)
    if rc != 0:
        return 'exit %d' % rc, cmd, out
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
    # Bosphorus reports every variable of its ring, which may exceed the
    # highest one used in an equation: the extra ones are free
    n = len(reported[0]) if reported else ring
    if any(len(s) != n for s in reported):
        return 'solutions of different lengths', cmd, out
    if n < ring:
        return 'solution covers %d variables, the input has %d' % (n, ring), cmd, out
    for s in reported:
        assign = dict(enumerate(s))
        if any(evaluate(p, assign) for p in polys):
            return 'a reported solution is not a solution', cmd, out
    P = pidx if pidx is not None else list(range(n))
    keys = set(tuple(s[i] for i in P) for s in reported)
    if len(keys) != len(reported):
        return 'two reported solutions agree on the %s' % ('projection set' if pidx else 'variables'), cmd, out
    cls, nv = anf_cnf(polys, n)
    expect = count(cls, nv, [i + 1 for i in P])
    if len(reported) != expect:
        return '%d solutions reported, ganak counts %d' % (len(reported), expect), cmd, out
    STATS['anf: allsol count'] += 1

    # 2) --anfwrite: the written ANF has the same count over the same
    # variables; without a projection set it has the same solutions
    outanf = unique_file('written', '.anf')
    cmd2 = [binary, '--anfread', path, '--anfwrite', outanf, '--verb', '0'] + opts
    rc, out2 = run(cmd2)
    if rc != 0:
        return 'anfwrite exit %d' % rc, cmd2, out2
    polys2 = read_written_anf(outanf)
    cls2, nv2 = anf_cnf(polys2, n)
    c2 = count(cls2, nv2, [i + 1 for i in P])
    if c2 != expect:
        return 'written ANF: %d projected models, expected %d' % (c2, expect), cmd2, out2
    if pidx is None:
        clsb, nvb = anf_cnf(polys + polys2, n)
        if count(clsb, nvb, [i + 1 for i in P]) != expect:
            return 'written ANF has different solutions', cmd2, out2
    STATS['anf: anfwrite count'] += 1

    # 3) --cnfwrite, of the input and of the written ANF: the count over the
    # CNF's 'c p show' line (all variables of the ring with --projshow 1)
    ps = 2 if pidx is not None else 1
    err = anf_count_via_cnf(binary, path, opts, ps, expect, 'anf: cnfwrite count')
    if err: return err
    err = anf_count_via_cnf(binary, outanf, opts, ps, expect, 'anf: anfwrite+cnfwrite count')
    if err: return err
    return None, cmd, out


def check_cnf(binary, rng):
    txt, nvars, cls, proj = rand_cnf(rng)
    path = unique_file('input', '.cnf')
    open(path, 'w').write(txt)
    opts = rand_opts(rng)
    P = proj if proj is not None else list(range(1, nvars + 1))
    expect = count(cls, nvars, P)

    # 1) --allsol: every model "v x(0) 1+x(1) ..." (ANF variable x(i) = CNF
    # variable i+1) satisfies the CNF, one per (projected) solution
    cmd = [binary, '--cnfread', path, '--solve', '--allsol', '--verb', '0'] + opts
    rc, out = run(cmd)
    if rc != 0:
        return 'exit %d' % rc, cmd, out
    sat = 's ANF-SATISFIABLE' in out
    if not sat and 's ANF-UNSATISFIABLE' not in out:
        return 'no answer', cmd, out
    if sat != (expect > 0):
        return 'wrong answer: ganak counts %d' % expect, cmd, out
    keys = []
    for m in re.finditer(r'^v (.*)$', out, re.M):
        assign = {}
        for tok in m.group(1).split():
            true = tok.startswith('1+')  # "1+x(i)" is true, a bare "x(i)" is false
            v = int(re.search(r'x\((\d+)\)', tok).group(1)) + 1
            if v <= nvars: assign[v] = true
        for c in cls:
            if not any(assign.get(abs(l), False) == (l > 0) for l in c):
                return 'model does not satisfy the CNF', cmd, out
        keys.append(tuple(assign.get(v, False) for v in P))
    if len(set(keys)) != len(keys):
        return 'two models agree on the %s' % ('projection set' if proj else 'variables'), cmd, out
    if len(keys) != expect:
        return '%d models reported, ganak counts %d' % (len(keys), expect), cmd, out
    STATS['cnf: allsol count'] += 1

    # 2) --cnfwrite: the count over the written 'c p show' line (the
    # projection set, or all input variables with --projshow 1)
    outcnf = unique_file('written', '.cnf')
    cmd2 = [binary, '--cnfread', path, '--cnfwrite', outcnf, '--verb', '0'] + with_projshow(opts, 2 if proj else 1)
    rc, out2 = run(cmd2)
    if rc != 0:
        return 'cnfwrite exit %d' % rc, cmd2, out2
    c = written_cnf_count(outcnf)
    if c is None:
        return 'written CNF has no projection line', cmd2, out2
    if c != expect:
        return 'written CNF: %d projected models, expected %d' % (c, expect), cmd2, out2
    STATS['cnf: cnfwrite count'] += 1

    # 3) --anfwrite: the count over the input's variables, then that ANF
    # through --cnfwrite, projected as the written ANF says
    outanf = unique_file('written', '.anf')
    cmd3 = [binary, '--cnfread', path, '--anfwrite', outanf, '--verb', '0'] + opts
    rc, out3 = run(cmd3)
    if rc != 0:
        return 'anfwrite exit %d' % rc, cmd3, out3
    polys = read_anf(outanf)
    acls, anv = anf_cnf(polys, nvars)
    c = count(acls, anv, P)
    if c != expect:
        return 'written ANF: %d projected models, expected %d' % (c, expect), cmd3, out3
    STATS['cnf: anfwrite count'] += 1
    ps = 2 if re.search(r'(?m)^c p show ', open(outanf).read()) else 1
    err = anf_count_via_cnf(binary, outanf, opts, ps, expect, 'cnf: anfwrite+cnfwrite count')
    if err: return err
    return None, cmd, out


def write_repro(cmd):
    """a script re-running the failing command, run from the fuzzer's directory"""
    path = unique_file('repro', '.sh')
    with open(path, 'w') as f:
        f.write('#!/bin/bash\nset -x\ncd "$(dirname "$0")/.."\n')
        f.write(' '.join(shlex.quote(c) for c in cmd) + '\n')
    os.chmod(path, 0o755)
    return path


def remove_files():
    for f in FILES:
        try: os.unlink(f)
        except OSError: pass
    del FILES[:]


def stop(signum, frame):
    # stopped mid-iteration (Ctrl-C, tmux kill-session): leave nothing behind
    remove_files()
    sys.exit(1)


def main():
    global GANAK
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, stop)
    ap = argparse.ArgumentParser()
    ap.add_argument('--iters', type=int, default=0, help='number of iterations, 0 (default): run until stopped')
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--bin', default=os.path.join(HERE, '..', 'build', 'bosphorus'))
    default_ganak = os.path.join(HERE, '..', '..', 'sat_solvers', 'ganak', 'build', 'ganak')
    if not os.access(default_ganak, os.X_OK): default_ganak = None
    ap.add_argument('--ganak', default=os.environ.get('GANAK') or default_ganak or shutil.which('ganak'),
                    help='ganak binary for the model counts (default: $GANAK, then '
                         '../../sat_solvers/ganak/build/ganak from the repository, then PATH)')
    a = ap.parse_args()
    if not a.ganak:
        sys.exit('fuzz: no ganak binary: set GANAK, pass --ganak or build ../sat_solvers/ganak next to this repository')
    GANAK = os.path.abspath(a.ganak) if os.path.exists(a.ganak) else a.ganak
    a.bin = os.path.abspath(a.bin)
    # out/ is next to this script wherever it is started from
    os.chdir(HERE)
    os.makedirs('out', exist_ok=True)
    base_seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    timeouts = i = 0
    start = time.time()
    while a.iters == 0 or i < a.iters:
        seed = base_seed + i
        i += 1
        rng = random.Random(seed)
        kind = 'cnf' if rng.random() < 0.35 else 'anf'
        del FILES[:]
        try:
            err, cmd, out = (check_cnf if kind == 'cnf' else check_anf)(a.bin, rng)
        except Timeout:
            # random inputs can be slow: skip them, it is not a failure
            timeouts += 1
            err = None
        except Exception:
            err, cmd, out = 'fuzzer exception', [sys.executable] + sys.argv, traceback.format_exc()
        if err:
            regen = 'cd %s && GANAK=%s ./fuzz.py --iters 1 --seed %d --bin %s' % (
                shlex.quote(HERE), shlex.quote(GANAK), seed, shlex.quote(a.bin))
            print('\nFAIL (%s input, seed %d): %s' % (kind, seed, err))
            print('  command:    %s' % ' '.join(shlex.quote(c) for c in cmd))
            print('  output tail:\n    ' + '\n    '.join(out.strip().splitlines()[-15:]))
            print('  files kept: %s' % ' '.join(os.path.join(HERE, f) for f in FILES))
            print('  re-run the failing command: %s' % os.path.join(HERE, write_repro(cmd)))
            print('  re-generate this iteration: %s' % regen, flush=True)
            sys.exit(1)
        remove_files()
        if i % 10 == 0:
            print('fuzz: %d iterations from seed %d, %d timed out, fuzz iterations/s: %.2f' % (
                i, base_seed, timeouts, i / (time.time() - start)), flush=True)
    print('fuzz: checks passed: ' + ', '.join('%s %d' % kv for kv in sorted(STATS.items())))
    print('fuzz: all %d iterations passed, base seed %d, %d timed out (skipped), fuzz iterations/s: %.2f' % (
        i, base_seed, timeouts, i / (time.time() - start)))

if __name__ == '__main__':
    main()
