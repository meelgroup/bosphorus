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

## Obtaining the Binary
Please use the [release binaries](https://github.com/meelgroup/bosphorus/releases)
or Nix: [install nix](https://nixos.org/download/) and then:
```shell
nix shell github:meelgroup/bosphorus
```
Then you will have the `bosphorus` binary available and ready to use.

Advanced users who want to build from source should
follow the steps of the [GitHub Actions build
workflow](https://github.com/meelgroup/bosphorus/blob/master/.github/workflows/build.yml):

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

Simplify it, write the simplified ANF and CNF, solve and write the solution
(a `.anf` input is read as ANF, a `.cnf` input as CNF):
```
$ ./bosphorus test.anf --anfwrite out.anf --cnfwrite out.cnf --solvewrite solution
```

The simplified ANF is in `out.anf`:
```
$ cat out.anf
x(2) + 1
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

## ANF rewrite rules and statistics

| rule | what it does |
|---|---|
| `anf-prop` | propagates units, (anti-)equivalences and `m+1` (all variables of `m` true) |
| `lin-gauss` | Gaussian elimination among the linear equations: drops redundant ones, shortens others, feeds units and equivalences to `anf-prop` |
| `binom-red` | reduces every equation modulo the monomial and binomial equations (`x*y = 0`, `x*y + x = 0`, definitions `x*y + z = 0`); degrees never grow |
| `prod-split` | `p = 0` where `1 + p` is a product of linear factors `(l1+c1)*(l2+c2)*...` becomes one linear equation per factor (every factor must be 1) |
| `poly-shorten` | replaces `p` by `p + f` when that is shorter (shortens XORs, re-uses definitions) |
| `mono-gauss` | Gaussian elimination with one column per monomial (linearisation): deletes equations that are combinations of others and replaces an equation by a lower-degree combination (a linear consequence of nonlinear equations); replacing by shorter combinations of the same degree is optional (11% smaller CNFs on the bivium family, slower CryptoMiniSat on ascon) |
| `lit-probe` | partial evaluation of small equations: forced literals, equivalences, binary implications and their SCCs |
| `var-probe` | failed-literal probing with propagation through the whole system: `x = 0` and `x = 1` are each propagated (units, `m+1`, products with one factor left), a failed branch forces `x`, agreeing branches set a variable, disagreeing ones make it equivalent to `x` (default: off) |
| `cnf-probe` | CryptoMiniSat's inprocessing on the CNF of the system, as Arjun runs it: equivalent-literal SCCs, probing of every variable, in-tree probing (no elimination); the fixed and the equivalent literals come back as equations, and with `--cnfprobebin` the binary clauses too (`a -> b` is the binomial `a*b + a = 0`; no effect measured on ascon, bivium or MQ, so off by default) |
| `fac-canon` | canonical linear factors of products modulo the linear span (default: off) |
| `fac-res` | resolution between products sharing a linear factor (default: off) |
| `gb-cone` | Gröbner bases of cones of small equations (the matrix-F4 engine per cone, plus a degree-bounded lex loop with `--gbfull 2`); linear members are added (`--gbconefactdeg`; quadratic ones pile up). On the ascon family the cones only change the three-round instances, and chaotically: default off for large systems, on for systems with few variables |
| `gb-split` | when the whole-system Gröbner basis needs a matrix over the cell budget, fixes a variable both ways and combines the bases of the two branches: an inconsistent branch forces the variable, members `f0`, `f1` with the same leading monomial combine to `f0 + x*(f0 + f1)`; fixing one variable of a random MQ system with n = 28 brings its degree of regularity from 5 back to 4 |
| `xl` | eXtended Linearization on the short equations |
| `elimlin` | ElimLin: elimination and substitution of linear equations, iterated |
| `sat-simp` | bounded CryptoMiniSat run; imports the units, equivalences and XORs it finds |

Every rule prints the size of the system before and after it ran
(`c [simp-stats]` lines, coloured on a terminal), and the run ends with a
table of totals per rule: calls, calls that changed something, time,
change in equations, monomials, linear equations, and variables set or
replaced. The `c Density:` line of the ANF stats gives the mean number of
terms and variables per equation, the fill (terms divided by the possible
monomials over the equation's own variables), the number of distinct
monomials and how often each is shared, and equations per variable.


## The Gröbner Engines
Complete bases come from Bosphorus's own matrix-F4 engine (`--gbengine 1`,
M4RI over squarefree 64-bit monomials, deterministic budgets `--gbsteps`
and `--gbmaxcells`); `--gbengine 0` uses BRiAl's `symmGB_F2` and
`--gbengine 2` a Matrix-F5 variant (with a MutantXL step: rows that fell
in degree are multiplied up again before the next degree) that is faster
on random quadratic systems and slower on structured ones. Both engines
stop as soon as the linear basis members fix every variable. A step whose
matrix would exceed `--gbmaxcells` (default 250 MB) is not built: the
system is split on a variable instead (`gb-split`, up to `--gbsplit`
levels, `--gbsplitrows` matrix rows in total), so memory stays bounded
and the degree stays low.

## ANF-to-CNF conversion strategies
Small polynomials are converted directly (Brickenstein's algorithm), and
small equations that share variables, such as the equations of one S-box,
are encoded jointly over the union of their variables. Larger ones are
linearised: nonlinear parts become CNF variables that are XORed together,
with small combinations such as `x*y + x` or `x*y + x*z` folded into one
variable each (the partner strategies of Jovanovic and Kreuzer). A product
of linear factors becomes one clause over one XOR-defined variable per
factor, which is how stream-cipher instances look as ANF. XORs are cut
into short pieces, or written as CryptoMiniSat's native xor clauses. A
projection set in the ANF becomes a `c p show` line over the same
variables in the CNF, so solution counts over it agree. `--quadsplit k`
encodes a quadratic equation with at most `k` products in its Dickson
decomposition `l1*l2 + l3*l4 + ... + linear` with one shared XOR-defined
variable per linear form and one per product, the way the raw CNFs of
cipher instances are written; on the ascon family this made CryptoMiniSat
slower on three of four instances (three seeds each), so it is off by
default.

## Multivariate quadratic (MQ) and HFE systems
Post-quantum multivariate schemes reduce to quadratic systems over GF(2).
Bosphorus reads [Fukuoka MQ challenge](https://www.mqchallenge.org/) files
and Magma polynomial lists such as the HFE systems of
`magma.maths.usyd.edu.au/users/allan/gb` through converters in `utils/`,
and can generate random MQ systems with a planted solution. A system with
few enough variables is solved by its Gröbner basis alone, without any
SAT solving.

Random MQ systems with m = 2n and a planted solution, one core (a 2020
laptop core), 200 s limit for the SAT solver. To reproduce a row:
```
python3 utils/mqgen.py 24 48 1 > mq24.anf   # n=24, m=48, seed 1
./build/bosphorus mq24.anf --solve              # F4; --gbengine 2 for F5
```

The degree of regularity of such a system is 4 up to n = 27 and 5 from
n = 28, and a degree-5 matrix has five times the columns of a degree-4
one; `gb-split` fixes one variable (two for n = 30, three for n = 32),
which brings every branch back to degree 4, and combines the branches.
Memory in parentheses.

| n | CryptoMiniSat on Bosphorus's CNF | BRiAl `symmGB_F2` | Bosphorus F4 | Bosphorus F5 |
|---|---|---|---|---|
| 16 | 0.7 s | 0.3 s | 0.1 s | 0.1 s |
| 20 | 1.4 s | 1.0 s | 0.2 s | 0.2 s |
| 24 | timeout | 15 s | 0.8 s (48 MB) | 1.0 s (158 MB) |
| 26 | timeout | | 3.7 s (103 MB) | 2.7 s (309 MB) |
| 28 | timeout | 196 s | 16 s (205 MB), was 187 s (1.7 GB) without the split | 13 s (511 MB) |
| 30 | timeout | | 52 s (488 MB) | 52 s (1.1 GB) |
| 32 | timeout | | 119 s (945 MB) | 134 s (2.1 GB) |
| 34 | timeout | | 603 s (1.5 GB) | 537 s (2.6 GB) |

HFE systems (secret degree 96) from Allan Steel's Magma page. Magma's F4
solved them in 2004 on hardware of that time (a 750 MHz UltraSPARC class
machine, roughly 20-30x slower than a current core), so its times are
what a far better engine achieved two decades ago; Bosphorus finds the
same solutions:

| system | Magma F4 (2004) | BRiAl `symmGB_F2` | Bosphorus F4 |
|---|---|---|---|
| HFE25 | 37 s | 166 s | 2.4 s (150 MB) |
| HFE30 | 114 s | 1329 s | 12 s (433 MB) |
| HFE35 | 543 s | no answer in 2 h | 167 s (3.9 GB) |

## List all solutions of an ANF

To find all solutions to `myfile.anf`:
```
./bosphorus myfile.anf --solve --allsol
[...]
s ANF-SATISFIABLE
v x(0) x(1)+1 x(2) x(3)
s ANF-SATISFIABLE
v x(0) x(1)+1 1+x(2) 1+x(3)
s ANF-UNSATISFIABLE
c Number of solutions found: 2
```

Where `x(0)` means `x(0)` must be FALSE and `x(1)+1` means `x(1)` must be TRUE.
This enumerates one solution per SAT call and does not scale beyond some
10000 solutions; count larger solution sets with ApproxMC as below.

## Counting solutions of an ANF
When there are too many solutions to list (say 2**40), count them on the
CNF: a projection set in the ANF (`c p show x1 x2 ... END`) is written into
the CNF as `c p show var1 var2 ... varn 0`, which
[ApproxMC](https://github.com/meelgroup/approxmc) understands:

```
./bosphorus test.anf --cnfwrite out.cnf
./approxmc out.cnf
[...]
c [appmc] Number of solutions is: 256*2**6
s mc 16384
```

If the number of solutions is low (say, less than 1000) you can also use
CryptoMiniSat to do the counting:

```
./cryptominisat5 --maxsol 100000 out.cnf
[...]
c Number of solutions found until now:    16384
s UNSATISFIABLE
```

## Mapping solutions from CNF to ANF
Write the CNF together with a solution map, solve the CNF with any SAT
solver, and map the model back to the ANF's variables:

```
./bosphorus test.anf --cnfwrite test.cnf --solmap solution_map
./cryptominisat5 test.cnf > cnf_solution
./scripts/map_solution.py solution_map cnf_solution
s ANF-SATISFIABLE
v x(0) 1+x(1) 1+x(2) x(3)
```

`x(0)` means `x(0)` is FALSE and `1+x(1)` means `x(1)` is TRUE.

## Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not
  run `bosphorus` on instances with over a million variables.
