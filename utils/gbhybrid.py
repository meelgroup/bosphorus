#!/usr/bin/env python3
"""Hybrid guess-and-Groebner solving of a GF(2) polynomial system (Bettale,
Faugere, Perret): fix k variables to each of their 2^k values and let
Bosphorus's whole-system Groebner basis (rule gb-cone) solve the remaining,
over-determined system. Deterministic: guesses are enumerated in Gray-code
order, the k variables are the ones occurring in most monomials.

  python3 utils/gbhybrid.py system.anf k [--bin build/bosphorus] [--wholevars N]

Prints the first solution found (as Bosphorus does) and the number of
guesses tried."""
import argparse, os, re, subprocess, sys, tempfile, time

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('anf'); ap.add_argument('k', type=int)
    ap.add_argument('--bin', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'build', 'bosphorus'))
    ap.add_argument('--wholevars', type=int, default=40)
    a = ap.parse_args()
    lines = [l for l in open(a.anf) if l.strip() and not l.startswith('c')]
    occ = {}
    for l in lines:
        for v in re.findall(r'x\(?(\d+)\)?', l):
            occ[int(v)] = occ.get(int(v), 0) + 1
    guess = sorted(occ, key=lambda v: (-occ[v], v))[:a.k]
    print('c guessing variables', ['x%d' % v for v in guess])
    t0 = time.time()
    tmp = tempfile.NamedTemporaryFile('w', suffix='.anf', delete=False)
    for g in range(1 << a.k):
        gray = g ^ (g >> 1)
        with open(tmp.name, 'w') as f:
            f.write(''.join(lines))
            for i, v in enumerate(guess):
                f.write('x%d%s\n' % (v, '' if (gray >> i) & 1 == 0 else ' + 1'))
        p = subprocess.run([a.bin, tmp.name, '--verb', '0', '--solve', '--gbwholevars', str(a.wholevars)],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        if 's ANF-SATISFIABLE' in p.stdout:
            sol = [l for l in p.stdout.splitlines() if l.startswith('v ')]
            print('s SATISFIABLE after %d guess(es), %.1f s' % (g + 1, time.time() - t0))
            print('\n'.join(sol))
            os.unlink(tmp.name); return 0
        if 's ANF-UNSATISFIABLE' not in p.stdout:
            print('c unexpected output for guess %d:\n%s' % (g, p.stdout[-500:])); os.unlink(tmp.name); return 2
    print('s UNSATISFIABLE after %d guesses, %.1f s' % (1 << a.k, time.time() - t0))
    os.unlink(tmp.name); return 1

if __name__ == '__main__':
    sys.exit(main())
