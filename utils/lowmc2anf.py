#!/usr/bin/env python3
"""Key-recovery instances of round-reduced LowMC as Bosphorus ANF, from the
matrices_and_constants_<n>_<r>.dat files of the LowMC cryptanalysis
challenge (github.com/lowmcchallenge/lowmcchallenge-material).

  python3 utils/lowmc2anf.py matrices_and_constants_129_4.dat 43 ROUNDS SEED > inst.anf

n = block size = key size, s = number of S-boxes per round (43 for the full
layer of the 129-bit instance, 1 or 10 for the partial layers), ROUNDS <=
the rounds in the file. A random key and plaintext are drawn from SEED, the
ciphertext is computed, and the ANF asks for the key: variables k1..kn
(projection set), and for every round and S-box the three S-box inputs
u<r>_<i> (defined by linear equations) and outputs y<r>_<i> (defined by the
three quadratic S-box equations). Everything else is linear in those.
The key is written as a comment for checking."""
import random, sys

def parse(path):
    lin, consts, keym = [], [], []
    cur = None
    for line in open(path):
        line = line.strip()
        if line.startswith('Linear layer ') and line.endswith(':'): cur = lin; cur.append([]); continue
        if line.startswith('Round constant ') and line.endswith(':'): cur = consts; cur.append([]); continue
        if line.startswith('Round key matrix ') and line.endswith(':'): cur = keym; cur.append([]); continue
        if line.startswith('[') and cur is not None:
            cur[-1].append([int(t) for t in line.strip('[]').replace(',', ' ').split()])
    consts = [c[0] for c in consts]
    return lin, consts, keym

def main(path, s, rounds, seed):
    lin, consts, keym = parse(path)
    n = len(lin[0])
    assert rounds <= len(lin), 'file has %d rounds' % len(lin)
    rng = random.Random(seed)
    key = [rng.randrange(2) for _ in range(n)]
    pt = [rng.randrange(2) for _ in range(n)]

    # --- numeric evaluation (reference implementation semantics)
    def matmul(vec, M): return [sum(M[i][j] & vec[j] for j in range(n)) & 1 for i in range(n)]
    def sbox(st):
        o = st[:]
        for i in range(s):
            a, b, c = st[3*i], st[3*i+1], st[3*i+2]
            o[3*i] = a ^ (b & c); o[3*i+1] = a ^ b ^ (a & c); o[3*i+2] = a ^ b ^ c ^ (a & b)
        return o
    st = [p ^ k for p, k in zip(pt, matmul(key, keym[0]))]
    for r in range(rounds):
        st = sbox(st)
        st = matmul(st, lin[r])
        st = [x ^ c for x, c in zip(st, consts[r])]
        st = [x ^ k for x, k in zip(st, matmul(key, keym[r+1]))]
    ct = st

    # --- symbolic: a linear form is (set of variable names, constant)
    kvars = ['k%d' % (i+1) for i in range(n)]
    def lin_key(M):  # round key bits as linear forms of the key variables
        return [(set(kvars[j] for j in range(n) if M[i][j]), 0) for i in range(n)]
    def add(a, b): return (a[0] ^ b[0], a[1] ^ b[1])
    def matmul_sym(forms, M):
        out = []
        for i in range(n):
            vs, c = set(), 0
            for j in range(n):
                if M[i][j]: vs ^= forms[j][0]; c ^= forms[j][1]
            out.append((vs, c))
        return out
    def fmt(vs, c):
        terms = sorted(vs) + (['1'] if c else [])
        return ' + '.join(terms) if terms else '0'
    eqs = []
    state = [add((set(), pt[i]), rk) for i, rk in enumerate(lin_key(keym[0]))]
    for r in range(rounds):
        new = state[:]
        for i in range(s):
            u = ['u%d_%d' % (r+1, 3*i+t+1) for t in range(3)]
            y = ['y%d_%d' % (r+1, 3*i+t+1) for t in range(3)]
            for t in range(3):
                vs, c = state[3*i+t]
                eqs.append(fmt(vs ^ {u[t]}, c))          # u = linear form
            a, b, c_ = u
            eqs.append('%s + %s*%s + %s' % (a, b, c_, y[0]))                       # y0 = a + b c
            eqs.append('%s + %s + %s*%s + %s' % (a, b, a, c_, y[1]))               # y1 = a + b + a c
            eqs.append('%s + %s + %s + %s*%s + %s' % (a, b, c_, a, b, y[2]))       # y2 = a + b + c + a b
            for t in range(3): new[3*i+t] = ({y[t]}, 0)
        state = matmul_sym(new, lin[r])
        state = [add(f, (set(), consts[r][i])) for i, f in enumerate(state)]
        state = [add(f, rk) for f, rk in zip(state, lin_key(keym[r+1]))]
    for i in range(n):
        vs, c = state[i]
        eqs.append(fmt(vs, c ^ ct[i]))
    print('c LowMC key recovery: n=%d s=%d rounds=%d seed=%d (from %s)' % (n, s, rounds, seed, path))
    print('c key: ' + ''.join(map(str, key)))
    print('c p show ' + ' '.join(kvars) + ' END')
    for e in eqs: print(e)

if __name__ == '__main__':
    main(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]))
