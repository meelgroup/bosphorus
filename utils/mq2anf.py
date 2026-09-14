#!/usr/bin/env python3
"""Convert a Fukuoka MQ challenge file over GF(2) (Type I / IV) to Bosphorus ANF.

The file has a header (field, n, m, seed, order) followed by m lines of 0/1
coefficients in graded reverse lexicographic order:
x1*x1, x1*x2, x2*x2, x1*x3, x2*x3, x3*x3, ..., x1, x2, ..., xn, 1
(x_i*x_i = x_i over GF(2)). Variables become x1..xn."""
import sys

def main(path, out):
    n = m = None
    lines = []
    with open(path) as f:
        for line in f:
            if line.startswith('Number of variables'): n = int(line.split(':')[1])
            elif line.startswith('Number of polynomials'): m = int(line.split(':')[1])
            elif line.startswith('Galois Field') and 'GF(2)' not in line.replace(' ', ''):
                sys.exit('only GF(2) instances can be converted')
            elif line[:1] in '01' and n is not None:
                lines.append([int(t) for t in line.replace(';', ' ').split()])
    assert n and m and len(lines) == m, (n, m, len(lines))
    # monomial list in the file's order
    mons = []
    for j in range(n):
        for i in range(j + 1):
            mons.append((i, j))
    mons += [(i,) for i in range(n)]
    mons.append(())
    with open(out, 'w') as o:
        o.write('c Fukuoka MQ challenge %s: GF(2), n=%d, m=%d\n' % (path, n, m))
        for coeffs in lines:
            assert len(coeffs) == len(mons), (len(coeffs), len(mons))
            terms = {}
            for c, mon in zip(coeffs, mons):
                if not c: continue
                key = tuple(sorted(set(mon)))  # x_i*x_i = x_i
                terms[key] = terms.get(key, 0) ^ 1
            txt = ' + '.join('1' if not k else '*'.join('x%d' % (v + 1) for v in k) for k, c in terms.items() if c)
            o.write((txt or '0') + '\n')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
