#!/usr/bin/env python3
"""Generate a random MQ system over GF(2) in Bosphorus ANF, like the Fukuoka
MQ challenge Type I (m = 2n, uniformly random quadratic polynomials) but
with a planted solution so that it is satisfiable:

  python3 utils/mqgen.py n m seed > sys.anf

Each polynomial has every monomial x_i*x_j (i<j), x_i and the constant with
probability 1/2, and the constant is then fixed so that the planted
assignment is a root."""
import random, sys

def main(n, m, seed):
    rng = random.Random(seed)
    sol = [rng.randrange(2) for _ in range(n + 1)]
    print('c random MQ system over GF(2): n=%d m=%d seed=%d (planted solution)' % (n, m, seed))
    for _ in range(m):
        terms = []
        val = 0
        for i in range(1, n + 1):
            for j in range(i + 1, n + 1):
                if rng.randrange(2):
                    terms.append('x%d*x%d' % (i, j)); val ^= sol[i] & sol[j]
        for i in range(1, n + 1):
            if rng.randrange(2):
                terms.append('x%d' % i); val ^= sol[i]
        if val: terms.append('1')
        print(' + '.join(terms) if terms else '0')

if __name__ == '__main__':
    main(int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]))
