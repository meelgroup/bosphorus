#!/usr/bin/env python3
"""Convert a Magma polynomial list over GF(2) (as in the HFE systems of
magma.maths.usyd.edu.au/users/allan/gb: `B := [ p1, p2, ... ];`) to Bosphorus
ANF. Field equations x^2+x are dropped (x^2 = x in a Boolean ring), other
powers are reduced, variables keep their names (x1..xn)."""
import re, sys

def main(path, out):
    txt = open(path).read()
    txt = re.sub(r'/\*.*?\*/', '', txt, flags=re.S)
    m = re.search(r'\bB\s*:=\s*\[(.*?)\];', txt, flags=re.S)
    if not m: sys.exit('no "B := [ ... ];" list found')
    polys = [p.strip() for p in m.group(1).replace('\n', '').split(',') if p.strip()]
    n_out = 0
    with open(out, 'w') as o:
        o.write('c converted from %s\n' % path)
        for p in polys:
            terms = {}
            for t in p.split('+'):
                t = t.strip()
                if not t: continue
                if t == '1': key = ()
                else:
                    vs = set()
                    for f in t.split('*'):
                        f = f.strip()
                        mm = re.fullmatch(r'(x\d+)(\^\d+)?', f)
                        if not mm: sys.exit('cannot parse factor %r in %r' % (f, p))
                        vs.add(mm.group(1))
                    key = tuple(sorted(vs, key=lambda s: int(s[1:])))
                terms[key] = terms.get(key, 0) ^ 1
            mons = [k for k, c in terms.items() if c]
            if not mons: continue  # x^2 + x = 0 in the Boolean ring
            o.write(' + '.join('1' if not k else '*'.join(k) for k in mons) + '\n')
            n_out += 1
    print('%s: %d polynomials written to %s' % (path, n_out, out))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
