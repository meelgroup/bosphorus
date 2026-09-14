[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
![build](https://github.com/meelgroup/bosphorus/workflows/build/badge.svg)

Bosphorus is an ANF simplification and solving tool. It takes as input an ANF
over GF(2) and can simplify and solve it. It uses many different algorithms,
including XL, SAT, Brickenstein's ANF-to-CNF conversion, Gauss-Jordan
elimination, etc. to simplify and solve ANFs.

The main use of the system is to simplify and solve ANF problems. It should
give you highly optimised ANFs and CNFs that it can solve. Its ANF
simplifications should be useful is many areas, not just direct ANF-to-SAT
solving. For example, it could be useful for helping to break [post-quantum
cryptograpy
problems](https://csrc.nist.gov/projects/post-quantum-cryptography).

This work was done by Davin Choo and Kian Ming A. Chai from DSO National
Laboratories Singapore, and Mate Soos and Kuldeep Meel from the National
University of Singapore (NUS). If you use Bosphorus, please cite our
[paper](https://www.cs.toronto.edu/~meel/Papers/date-cscm19.pdf)
([bibtex](https://www.cs.toronto.edu/~meel/bib/CSCM19.bib)) published at DATE
2019. Some of the code was generously donated by [Security Research Labs,
Berlin](https://srlabs.de/).


## Compiling
Use of the [release
binaries](https://github.com/meelgroup/bosphorus/releases) is _strongly_
encouraged. The second best thing to use is Nix. Simply [install
nix](https://nixos.org/download/) and then:
```shell
nix shell github:meelgroup/bosphorus
```

Then you will have the `bosphorus` binary available and ready to use.

## ANF simplification and solving
Suppose we have a system of two equations:
```
x1 ⊕ x2 ⊕ x3 = 0
x1 * x2 ⊕ x2 * x3 + 1 = 0
```

Put this in the ANF file `test.anf`:
```
$ cat test.anf
x1 + x2 + x3
x1*x2 + x2*x3 + 1
```
or, you can use the more detailed description:
```
$ cat test-detail.anf
x(1) + x(2) + x(3)
x(1)*x(2) + x(2)*x(3) + 1
```

Let's simplify, output a simplified ANF, a simplified CNF, solve it and write
out the solution:
```
$ ./bosphorus --anfread test.anf --anfwrite out.anf --cnfwrite out.cnf --solvewrite solution
```

The input file can also be given as a plain positional argument: a file ending
in `.anf` is read as ANF and one ending in `.cnf` is read as CNF, so the above
is the same as `./bosphorus test.anf --anfwrite out.anf ...`.

The simplified ANF is in `out.anf`:
```
$ cat out.anf
c -------------
c Fixed values
c -------------
x(2) + 1
c -------------
c Equivalences
c -------------
x(3) + x(1) + 1
c UNSAT : false
```

The simplified CNF is in `out.cnf`:
```
3 0
2 4 0
-2 -4 0
```
This CNF represents all the solutions to the ANF, i.e. it's equivalent to the
ANF.


A solution to the problem is in `solution`:
```
$ cat solution
v -0 1 2 -3
```
This means x0 is `false`, x1 is `true`, x2 is `true` and x3 is `false`.

Explanation of simplifications performed:
* The first linear polynomial rearranged to `x1 = x2 + x3` to eliminate x1 from the other equations
* The second polynomial becomes `(x2 + x3) * x2 + x2 * x3 + 1 = 0`, which simplifies to `x2 + 1 = 0`
* Substituting `x2 + 1 = 0` yields `x1 + x3 + 1 = 0`

## ANF rewrite rules and statistics

Simplification runs a set of rewrite rules in rounds until nothing changes
any more. The cheap in-place rules come first in every round, then the
strategies that work on a copy of the system and feed back what they learnt:

| rule | what it does | switch |
|---|---|---|
| `anf-prop` | propagates units (`x`, `x+1`), (anti-)equivalences (`x+y`, `x+y+1`) and `m+1` (all variables of monomial `m` are true) through the system | always on |
| `lin-gauss` | Gaussian elimination among the linear equations only, shortest first: an equation that is a combination of shorter ones is deleted, one whose reduced form is shorter is replaced by it, and reduced forms with one or two variables are units and equivalences for `anf-prop`. Nonlinear equations are not touched | `--lingauss 0/1` |
| `binom-red` | reduces every equation modulo the monomial and binomial equations: `x*y = 0` deletes every monomial divisible by `x*y`, `x*y + x = 0` (x implies y) turns `x*y*z` into `x*z`, and a definition `x*y + z = 0` lowers the degree of every monomial containing `x*y`. The degree-lexicographic leading term is rewritten, so degrees never grow | `--binomred 0/1`, `--binomredlen N` uses equations of up to N terms as rules (default 2) |
| `poly-shorten` | replaces an equation `p` by `p + f` whenever the two share more than half of the terms of `f`, so the result is shorter. Shortens XORs and re-uses definitions (`y + x1*x2 + x3` in the system rewrites `x1*x2 + x3 + ...` to `y + ...`) | `--shorten 0/1` |
| `lit-probe` | partial evaluation of small equations: `p|x=0 == 1` forces `x = 1`, the four evaluations on a pair of variables give equivalences (`x*y + x + 1` gives `x = 1, y = 0`; `x*y*(z+1) + 1` gives `x = y = 1, z = 0`) and binary implications; the strongly connected components of the implication graph give further equivalences (`x*y + x` with `x*y + y` gives `x = y`) | `--probe 0/1`, `--probevars N` only looks at equations with at most N variables (default 8) |
| `fac-canon` | for equations that are products of linear factors: reduces every factor modulo the span of the linear equations and uses the shortest representative of its class, so equal constraints become identical and short. Off by default: it makes the CNF ~15% smaller but CryptoMiniSat slower on average on the bivium family | `--faccanon 0/1` |
| `fac-res` | resolution between two products sharing a linear factor with opposite constants: `(A+a)*R1 = 0` and `(A+a+1)*R2 = 0` give `R1*R2 = 0`; a one-factor resolvent is a new linear equation. Off by default | `--facres 0/1`, `--facresmax N` |
| `gb-window` | degree-bounded Gröbner bases (BRiAl) of windows of small equations sharing variables; short linear members of the bases (at most `--gbmaxfactvars` variables) are added. Off by default and experimental: on the bivium instances it only finds long linear consequences, which do not help. The work is bounded by a number of S-polynomials, not by time | `--gb 0/1`, `--gbdeg`, `--gbwindow`, `--gbmaxlen`, `--gbsteps`, `--gbmaxfactvars` |
| `xl` | eXtended Linearization: multiplies equations by variables and Gauss-Jordan eliminates, learning linear equations; only sees equations with at most `--xlmaxlen` terms | `--xl 0/1`, `--xldeg`, `--xlsample`, `--xlmaxlen` |
| `elimlin` | ElimLin: Gauss-Jordan elimination and substitution of the linear equations found, iterated | `--el 0/1`, `--elsample` |
| `sat-simp` | converts to CNF, runs CryptoMiniSat for a bounded number of conflicts and imports the units, binary XORs and recovered XORs it found | `--sat 0/1`, `--satinc`, `--satlim` |

`--rewrite 0` turns off all in-place rules at once. A strategy that learns
nothing twice in a row is not run again. Every rule is sound: the
in-place rules only ever add a multiple of another equation still in the
system to an equation, or add a fact implied by a single equation, so the
solution set over all variables is unchanged.

Before and after every rule the size of the system is printed, in the style
of CryptoMiniSat's `[simp-stats]` lines, so it is easy to see what each rule
achieved:

```
c [simp-stats] bef binom-red              eqs 2294 monoms 288610 lin_eqs 314 nonlin_eqs 1980 max_deg 3
c [simp-stats]                            free_vars 789 set_vars 97 repl_vars 0 mem_MB 60 T: 5.58 depth 0
c [binom-red] rewrote 1427 eqs (59364 monomials) T: 2.95
c [simp-stats] aft binom-red              eqs 2225 monoms 261892 lin_eqs 355 nonlin_eqs 1870 max_deg 3
c [simp-stats]                            free_vars 789 set_vars 97 repl_vars 0 mem_MB 62 T: 8.53 T-step: 2.95 depth 0
```

On a terminal the rule name is orange, numbers that went down are green and
numbers that went up are red (`--color 0/1/2` = never/always/auto; the
`NO_COLOR` environment variable is honoured). `depth` is the nesting: a rule
that runs inside another one (e.g. propagation of what XL learnt) is indented
and shown at verbosity 2 and above.

### ANF-to-CNF conversion strategies

When a polynomial is too large for Brickenstein's direct conversion
(`--karn`), it is linearised: every nonlinear part becomes a CNF variable and
the parts are XORed together with the cutting number `--cutnum`. Instead of
one CNF variable per monomial (the *standard strategy*), Bosphorus uses the
*partner strategies* of Jovanovic and Kreuzer, "Algebraic Attacks using
SAT-Solvers" (Groups Complexity Cryptology, 2010), which fold small
combinations of terms into one variable with a short clause set each:

| combination | name | CNF variable `y` |
|---|---|---|
| `x*y + x` | linear partner (LPS) | `y = x & !y` (3 clauses) |
| `x*y + x + y + 1` | double partner (DPS) | `y = !x & !y` (3 clauses) |
| `x*y + x*z` | quadratic partner (QPS) | `y = x & (y ^ z)` (5 clauses) |
| `x*y*z + x*y*w` | cubic partner (CPS) | `y = x & y & (z ^ w)` (6 clauses) |

All of these are of the form `P * h(F)`, a common monomial `P` times a small
polynomial `h` in a few free variables `F`; the cover is searched for
generally, so mixtures such as `x*(y + z + 1)` and `x*(y+1)*(z+1)` are found
too, and the search accounts for monomials that already have a CNF variable
from another polynomial. `--partner 0` restores the standard strategy. The
`[cnf-stats]` line reports the number of variables, clauses, literals and
how many monomial, chunk (partner) and XOR-cut variables were introduced;
with `--comments 1` every auxiliary variable's meaning is written into the
CNF as a comment.

With `--karncluster N` small nonlinear equations that share variables (the
five output equations of an S-box, say) are encoded jointly: the forbidden
assignments of all equations of a cluster are covered by one clause set
(Brickenstein's algorithm) over the union of their variables, as long as
that union has at most `N` variables, so the clauses propagate across the
equations of the cluster. Off by default.

The `c p show` line naming the original variables is written only when the
CNF has at least as many XOR-cut variables as original variables
(`--projshow 2`, the default; `1` always, `0` never). CryptoMiniSat treats
the listed variables as a sampling set: it never eliminates them and skips
Gauss-Jordan on matrices that contain few of them. On the bivium family,
whose long XORs are cut into thousands of pieces, that skipped Gauss-Jordan
is the difference between 7 seconds and a timeout; on the ascon family,
whose XORs are short, the line makes solving 2-3x slower.

#### Products of linear factors and native XOR clauses

Before any of the above, a polynomial that is a product of linear factors,
`(l1 + c1) * (l2 + c2) * (l3 + c3)`, is recognised (`--factor`, default on).
Such a polynomial is 0 exactly when one factor is, so it is encoded as one
clause "`l1 = c1` or `l2 = c2` or `l3 = c3`" over one shared CNF variable
per multi-variable factor, each defined by a single XOR. This is how e.g.
the bivium/Trivium keystream instances of the XNF solver benchmarks look
once written as ANF (a product of three linerals of 50 variables each has
125000 terms, which the monomial-per-variable encoding cannot handle). The
input is still ANF and the output still CNF; the factorisation is tracked
through the simplification so it survives substitutions, and in
product-preserving mode (`--keepfactor`, auto-detected) the in-place rules
leave such products alone.

By default every XOR is cut into pieces of `--cutnum` variables and written
as plain clauses. With `--xorcls 1` the XORs are instead written as
CryptoMiniSat's native xor clauses (`x 1 2 3 0` lines, optionally chained
into pieces of `--xormaxlen`); the output is then CNF-XOR, which only
CryptoMiniSat reads. The built-in solver (`--solve`, `--solve-xnf`) and the
SAT-based simplification always use native xor clauses. On the bivium
instances plain cutting at 5 was as good as or better than native XORs.

### Example: the bivium keystream instances

The bivium instances of the XNF solver benchmarks (state recovery from 354 or
531 keystream bits with 26-50 known state bits) are products of linear factors
once written as ANF. With Bosphorus's output (`bosphorus X.anf --el 0
--cnfwrite X.cnf`, about 3 seconds per instance) CryptoMiniSat (`--sls 0
--autodisablegauss 0 --presimp 1 --maxmatrixrows 100000 --maxmatrixcols
100000 --maxnummatrices 1000000 --minmatrixrows 1`, 200 s, models checked
against the original ANF) solves the 13 instances the benchmark's own CNF
solves, 1.5-4x faster on the ones that take more than a few seconds, plus
tmp_ifs1zce. Of the remaining seven hard instances (26-33 known bits at 531
steps, or 26 at 354) three more are solved with some solver seed from
Bosphorus's output and none from the benchmark CNF, but on these the
outcome is dominated by the seed: the same CNF solves in 10 s with one seed
and times out with the next, so single runs mean little. Times below are
with the default seed, and with seeds 1 and 2 for the hard ones (T =
timeout).

| instance | steps | known bits | benchmark CNF | Bosphorus output |
|---|---|---|---|---|
| tmpdvd4qqhc | 354 | 26 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: T/T) |
| tmp0pckmywp | 354 | 34 | 54 s | 14 s |
| tmpafl2snvs | 354 | 35 | 36 s | 21 s |
| tmpc2byc16q | 354 | 37 | 11 s | 8 s |
| tmp44mkq4l9 | 354 | 40 | 5 s | 6 s |
| tmp6ifbfbz4 | 354 | 43 | 4 s | 5 s |
| tmp72yj0xcp | 354 | 46 | 4 s | 5 s |
| tmp09lh13kb | 354 | 49 | 3 s | 3 s |
| tmp_utof4mt | 354 | 50 | 3 s | 3 s |
| tmpbi2n8e6d | 531 | 26 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: T/T) |
| tmp0c_s736b | 531 | 27 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: T/T) |
| tmpec79lh8f | 531 | 28 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: T/T) |
| tmp65a5rlro | 531 | 29 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: T/T) |
| tmpcatw2met | 531 | 30 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: 52/T) |
| tmp4grsp1np | 531 | 31 | timeout (seeds 1,2: T/T) | timeout (seeds 1,2: 15/T) |
| tmp3ce4vlbu | 531 | 33 | 53 s (seeds 1,2: T/T) | timeout (seeds 1,2: 20/10) |
| tmp_ifs1zce | 531 | 35 | timeout | 6 s |
| tmp2v65y1ui | 531 | 43 | 16 s | 7 s |
| tmp9_e6244z | 531 | 46 | 10 s | 6 s |
| tmp7akxbh30 | 531 | 49 | 10 s | 6 s |
| tmpb26sj1pd | 531 | 50 | 12 s | 6 s |

What makes the difference on this family is not the clause set but
CryptoMiniSat's Gauss-Jordan: the harness flags force it on, the cut XORs
of both encodings form matrices of 5000-10000 rows, and with those in use
CryptoMiniSat needs more than 200 s on tmp_ifs1zce with either CNF. With
Gauss-Jordan off (`--maxmatrixrows 0`) the Bosphorus CNF solves in 9 s.
The `c p show` line that Bosphorus writes for such CNFs (see `--projshow`)
lists the original variables as a sampling set, and CryptoMiniSat only uses
a matrix when at least 60% of the sampling variables occur in it, which is
what skipped the matrices in the runs of the table (58.8% of them did). The
margin is thin: a slightly different encoding of the same instance lands at
61% and times out.

### Example: the ascon key-recovery instances

The ascon instances of the same benchmark set (key recovery from the state
after 2-4 initialization rounds, named variables, 3136 variables, 1280
quadratic S-box equations) are the opposite case: short XORs, S-box
equations of 6 variables that Brickenstein's conversion encodes directly.
Bosphorus's rewriting takes 3 seconds; XL used to re-learn the linear part
of the S-box equations every iteration, which `lin-gauss` and the span
filter of learnt facts now stop. CryptoMiniSat times with three seeds
(min/median/max, 200 s limit, models checked) on the four-round instances
the benchmark's CNF solves within the limit:

| instance | benchmark CNF | Bosphorus output | `--karncluster 10` |
|---|---|---|---|
| tmp3g3f82vv | 12.4/13.7/26.6 | 12.2/16.4/24.4 | 9.0/13.7/23.0 |
| tmpgmh2blh0 | 47.2/66.2/89.5 | 20.4/38.0/90.4 | 16.7/36.5/123.4 |
| tmpn1uaqlcc | 7.0/8.0/8.1 | 1.6/1.9/2.1 | 0.5/2.3/4.0 |
| tmpdchqvtq0 | 8.9/15.7/40.3 | 8.8/9.0/24.4 | 5.0/9.6/10.1 |
| tmppdzw6ahj | 11.6/13.5/14.5 | 6.5/6.9/12.5 | 5.1/6.4/9.5 |
| tmpv1bh0ebt | 11.9/12.3/12.9 | 11.5/26.1/48.4 | 19.8/24.0/24.9 |
| tmpp4b0ewm7 | 9.3/11.5/14.6 | 5.0/6.8/7.1 | 3.8/6.1/7.9 |
| tmpborqf5jg | 17.1/79.1/117.8 | 36.9/47.1/84.8 | 27.6/45.8/96.0 |
| tmpt2t5c67b (3 rounds) | 6.5/6.8/7.3 | 4.0/5.1/8.0 | 0.8/0.9/0.9 |
| tmpvxk1t18u (3 rounds) | 8.1/8.6/9.7 | 9.7/15.3/33.8 | 14.6/23.5/27.2 |
| tmpwchcc7lm (3 rounds) | 7.5/8.8/9.5 | 14.6/26.8/43.7 | 5.6/34.5/69.0 |
| tmp94o0gmwt (3 rounds) | 8.7/9.0/10.4 | 14.8/15.6/19.8 | 27.6/30.7/43.0 |

Geometric mean of the medians: 14.4 s for the benchmark CNF, 12.8 s for
Bosphorus's output, 12.1 s with the cluster encoding. The instances that
time out with the benchmark CNF time out with Bosphorus's output too.

## List all solutions of an ANF

To find all solutions to `myfile.anf`:
```
./bosphorus \
    --anfread myfile.anf \
    --cnfwrite myfile.cnf \
    --solve --allsol
[...]
s ANF-SATISFIABLE
v x(0) x(1)+1 x(2) x(3)
s ANF-SATISFIABLE
v x(0) x(1)+1 1+x(2) 1+x(3)
s ANF-UNSATISFIABLE
c Number of solutions found: 2
```

Where `x(0)` means `x(0)` must be FALSE and `x(1)+1` means `x(1)` must be TRUE.

To convert `myfile.anf` to `myfile.cnf` with all the simplifications:

## Counting solutions of an ANF

Sometimes, there are too many solutions to an ANF to list them all (e.g.
2**40). You can count the number of solutions of an ANF in `test.anf` by using
the standard translation and taking advantage of the projection written inside
the CNF. This projection set is written as `c p show var1 var2 ... varn 0`. Many
counters, such as [ApproxMC](https://github.com/meelgroup/approxmc) are able to
use this format to count the solutions in the CNF. Here is how to do it with
ApproxMC:

```
./bosphorus --anfread test.anf --cnfwrite out.cnf
./approxmc out.cnf
[...]
c [appmc] Number of solutions is: 256*2**6
s mc 16384
```

If the number of solutions is low (say, less than 1000) you can also use
CryptoMiniSat to do the counting:

```
./bosphorus --anfread test.anf --cnfwrite out.cnf
./cryptominisat --maxsol 100000 out.cnf
[...]
c Number of solutions found until now:    16384
s UNSATISFIABLE
```

## CNF simplification

This usage of the tool is **EXPERIMENTAL**. Do not, under any circumstances,
rely on its correctness or veracity. In general `--cnfread` is not
well-supported. If you are still interested, then Bosphorus can simplify and
solve CNF problems. When simplifying or solving CNF problems, the CNF is
(extremely) naively translated to ANF, then simplifications are applied, and a
sophisticated system then translates the ANF back to CNF. This CNF can then be
optinally solved.

Let's say you have the CNF:

```
$ cat test.cnf
-2  3  4 0
 2 -3 0
 2  3 -4 0
-2 -3 -4 0
 1  5 0
-1 -5 0
```

Let's simplify and get the ANF:
```
$ ./bosphorus --cnfread test.cnf --anfwrite out2.anf
$ cat out2.anf
x(1)*x(2)*x(3) + x(1)*x(2) + x(1)*x(3) + x(1)
x(1)*x(2)*x(3) + x(1)*x(2) + x(2)*x(3) + x(2)
x(1)*x(2) + x(1) + x(2) + 1
x(1)*x(2)*x(3)
x(1) + x(2) + x(3)
c -------------
c Equivalences
c -------------
x(4) + x(0) + 1

```

The system recovered XOR `x(1) + x(2) + x(3)` using ElimLin from the top 4
equations that encode the CNF's first 4 clauses. This resoution is in fact
non-trivial, and can lead to interesting facts that can then be re-injected
back into the CNF. Note that the first 4 clauses encode an XOR because the 2nd
clause can be extended to the weaker clause `2 -3 4 0`, giving the trivial
encoding of `x(1) + x(2) + x(3)` in CNF.

## Mapping solutions from CNF to ANF

Let's take a simple ANF:

```
$ cat test.anf
x(1) + x2 + x3
x1*x2 + x2*x3 + 1
```

Let's simplify and it to CNF:

```
./bosphorus --anfread test.anf  --cnfwrite test.cnf --solmap solution_map
```

Let's solve with any SAT solver:

```
lingeling test.cnf > cnf_solution
```

Let's map the CNF solution back to ANF using the python script under `utils/map_solution.py`:

```
./map_solution.py solution_map cnf_solution
c solution below, with variables starting at 0, as per ANF convention.
s ANF-SATISFIABLE
v x(0) 1+x(1) 1+x(2) x(3)
```

This means that `x(0)=FALSE`, `x(1)=TRUE`, `x(2)=TRUE`, and `x(3)=FALSE`.

If you want all solutions:

```
./cryptominisat x --maxsol 10000000 > cnf_solutions
```

Then take the solutions from `cnf_solutions` individually, put them in a file,
and call `map_solution` on it, as before.

## Building from source

The build uses CMake and automatically fetches and compiles CryptoMiniSat (and
in turn its own dependencies), so the only C++ dependencies you need to provide
are zlib, GMP, m4ri and BRiAl (whose headers need Boost). Install the system
packages:

```bash
# Debian/Ubuntu
sudo apt-get install build-essential cmake pkg-config git zlib1g-dev libgmp-dev \
                     libboost-dev

# macOS (brew)
brew install cmake pkg-config automake libtool boost gmp
```

Neither [m4ri](https://github.com/malb/m4ri) nor
[BRiAl](https://github.com/BRiAl/BRiAl) is packaged on current Ubuntu or in
Homebrew, so install them from their release tarballs. Then build Bosphorus:
```bash
git clone --recurse-submodules https://github.com/meelgroup/bosphorus
cd bosphorus
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

If the above is complicated, please use the release binaries, or Nix, as
described above.

### Testing
The test suite is driven by [lit](https://pypi.org/project/lit/) and checks
Bosphorus end-to-end: solutions and written CNFs are verified against a
brute-forced ground truth computed independently of Bosphorus.
```shell
pip install lit
cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTING=ON ..
cmake --build .
ctest --verbose
```

## Fuzzing
`utils/fuzz.py` generates random small ANF and CNF inputs with random option
settings and checks every answer against brute force (all solutions for ANF
input, the SAT/UNSAT answer and the model for CNF input, and the solutions of
the ANF written by `--anfwrite`):

```
python3 utils/fuzz.py --iters 60          # about 15 seconds
python3 utils/fuzz.py --iters 1 --seed N  # replay one case
```

A failing input is kept as `fuzz-fail-<seed>.anf` or `.cnf` together with the
command line that failed. Run it before committing.

## Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not
  run `bosphorus` on instances with over a million variables.
