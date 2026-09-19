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

Bosphorus takes an input and at most one output file, and tells ANF from
CNF by the `.anf`/`.cnf` extension; inputs may be gzipped (`.anf.gz`,
`.cnf.gz`). Without an output file it solves:
```
$ ./bosphorus test.anf              # simplify and solve
$ ./bosphorus test.anf out.cnf      # simplify and write the CNF
```

To write both the simplified ANF and CNF, and solve and write the solution:
```
$ ./bosphorus test.anf out.anf --cnfwrite out.cnf --solvewrite solution
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

## ANF rewrite rules

| rule | what it does |
|---|---|
| `anf-prop` | Propagates units, (anti-)equivalences and `m+1` (a monomial that must be 1 sets all its variables). |
| `lin-gauss` | Gaussian elimination among the linear equations; the units and equivalences it finds go to `anf-prop`. |
| `binom-red` | Reduces every equation modulo the monomial and binomial equations (`x*y = 0`, `x*y + x = 0`, `x*y + z = 0`). |
| `prod-split` | A product of linear factors that must be 1 becomes one linear equation per factor. |
| `poly-shorten` | Replaces `p` by `p + f` when that is shorter. |
| `mono-gauss` | Gaussian elimination with one column per monomial (linearisation): deletes dependent equations and replaces equations by lower-degree combinations of others. |
| `lit-probe` | Partial evaluation of small equations: forced literals, equivalences, binary implications and their SCCs. |
| `var-probe` | Failed-literal probing [[1]](#references) through the whole system: what both branches of `x` force is kept (default: off). |
| `cnf-probe` | CryptoMiniSat's inprocessing on the CNF of the system, as Arjun [[2]](#references) runs it; the fixed and equivalent literals come back as equations. |
| `fac-canon` | Canonical linear factors of products modulo the linear span (default: off). |
| `fac-res` | Resolution between products sharing a linear factor (default: off). |
| `gb-cone` | Gröbner bases (F4 [[3]](#references)) of cones of small equations; the linear members are added (default: on for small systems). |
| `gb-split` | Fixes a variable both ways when the whole-system Gröbner basis would exceed the matrix budget, and combines the two bases; a fixed variable lowers the degree of regularity [[5]](#references). |
| `xl` | eXtended Linearization [[6]](#references) on a random sample of the short equations (default: off; it learnt nothing new on bivium, ascon and MQ). |
| `elimlin` | ElimLin [[7]](#references): elimination and substitution of linear equations, iterated. |
| `sat-simp` | Bounded CryptoMiniSat run; imports the units, equivalences and XORs it finds. |

## The Gröbner Engines
Complete bases come from Bosphorus's own matrix-F4 engine (`--gbengine 1`,
M4RI over squarefree 64-bit monomials, deterministic budgets `--gbsteps`
and `--gbmaxcells`); `--gbengine 0` uses BRiAl's `symmGB_F2` [[8]](#references) and
`--gbengine 2` a Matrix-F5 [[4]](#references) variant (with a MutantXL [[9]](#references) step: rows that fell
in degree are multiplied up again before the next degree) that is faster
on random quadratic systems and slower on structured ones. Both engines
stop as soon as the linear basis members fix every variable. A step whose
matrix would exceed `--gbmaxcells` (default 250 MB) is not built: the
system is split on a variable instead (`gb-split`, up to `--gbsplit`
levels, `--gbsplitrows` matrix rows in total), so memory stays bounded
and the degree stays low.

## ANF-to-CNF conversion strategies
Small polynomials are converted directly (Brickenstein's algorithm
[[10]](#references)), and small equations that share variables, such as
those of one S-box, are encoded jointly. Larger ones are linearised:
nonlinear parts become XORed CNF variables, with small combinations such
as `x*y + x` folded into one variable (the partner strategies of Jovanovic
and Kreuzer [[11]](#references)). A product of linear factors becomes one
clause over one XOR-defined variable per factor. XORs are cut into short
pieces, or written as CryptoMiniSat's native XOR clauses.

A projection set in the ANF becomes a `c p show` line in the CNF, so
solution counts over it agree.

## Multivariate quadratic (MQ) and HFE systems
Post-quantum multivariate schemes reduce to quadratic systems over GF(2).
Bosphorus reads [Fukuoka MQ challenge](https://www.mqchallenge.org/) files
and Magma polynomial lists such as the
[HFE systems of Allan Steel](http://magma.maths.usyd.edu.au/users/allan/gb/)
through converters in `utils/`,
and can generate random MQ systems with a planted solution. A system with
few enough variables is solved by its Gröbner basis alone, without any
SAT solving.

To reproduce a row of the table below:
```
python3 utils/mqgen.py 24 48 1 > mq24.anf   # n=24, m=48, seed 1
./build/bosphorus mq24.anf                  # F4; --gbengine 2 for F5
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
| 28 | timeout | 196 s | 16 s (205 MB) | 13 s (511 MB) |
| 30 | timeout | | 52 s (488 MB) | 52 s (1.1 GB) |
| 32 | timeout | | 119 s (945 MB) | 134 s (2.1 GB) |
| 34 | timeout | | 603 s (1.5 GB) | 537 s (2.6 GB) |

HFE systems (secret degree 96): Magma's F4 solved them in 2004 on hardware
roughly 20-30x slower than a current core; Bosphorus finds the same
solutions:

| system | Magma F4 (2004) | BRiAl `symmGB_F2` | Bosphorus F4 |
|---|---|---|---|
| HFE25 | 37 s | 166 s | 2.4 s (150 MB) |
| HFE30 | 114 s | 1329 s | 12 s (433 MB) |
| HFE35 | 543 s | no answer in 2 h | 167 s (3.9 GB) |

## List all solutions of an ANF

To find all solutions to `myfile.anf`:
```
./bosphorus myfile.anf --allsol
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
CNF with [ApproxMC](https://github.com/meelgroup/approxmc). The ANF line
`c p show ... END` sets the projection set; it becomes `c p show ... 0` in the CNF:

```
$ cat count.anf
c p show x1 x2 x3 x4 END
x1*x2 + x3 + 1
x3*x5 + x6
$ ./bosphorus count.anf out.cnf
$ ./approxmc out.cnf
[...]
c [appmc] Number of solutions is: 4*2**0*2
s mc 8
```

If the number of solutions is low (say, less than 1000) you can also use
CryptoMiniSat to do the counting:

```
./cryptominisat5 --maxsol 100000 out.cnf
[...]
c Number of solutions found until now:      8
s UNSATISFIABLE
```

## Mapping solutions from CNF to ANF
Write the CNF together with a solution map, solve the CNF with any SAT
solver, and map the model back to the ANF's variables:

```
./bosphorus test.anf test.cnf --solmap solution_map
./cryptominisat5 test.cnf > cnf_solution
./scripts/map_solution.py solution_map cnf_solution
s ANF-SATISFIABLE
v x(0) 1+x(1) 1+x(2) x(3)
```

`x(0)` means `x(0)` is FALSE and `1+x(1)` means `x(1)` is TRUE.

## Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not
  run `bosphorus` on instances with over a million variables.

## References
1. J. W. Freeman, [Improvements to propositional satisfiability search algorithms](https://repository.upenn.edu/items/fb5a3fa3-3107-4be6-9ed4-7dd95ba84d27), PhD thesis, University of Pennsylvania, 1995.
2. M. Soos, K. S. Meel, [Arjun: an efficient independent support computation technique and its applications to counting and sampling](https://arxiv.org/abs/2110.09026), ICCAD 2022.
3. J.-C. Faugère, [A new efficient algorithm for computing Gröbner bases (F4)](https://doi.org/10.1016/S0022-4049(99)00005-5), J. Pure Appl. Algebra 139, 1999.
4. J.-C. Faugère, [A new efficient algorithm for computing Gröbner bases without reduction to zero (F5)](https://doi.org/10.1145/780506.780516), ISSAC 2002.
5. M. Bardet, J.-C. Faugère, B. Salvy, [On the complexity of the F5 Gröbner basis algorithm](https://doi.org/10.1016/j.jsc.2014.09.025), J. Symb. Comput. 70, 2015.
6. N. Courtois, A. Klimov, J. Patarin, A. Shamir, [Efficient algorithms for solving overdefined systems of multivariate polynomial equations](https://doi.org/10.1007/3-540-45539-6_27), EUROCRYPT 2000.
7. N. Courtois, P. Sepehrdad, P. Sušil, S. Vaudenay, [ElimLin algorithm revisited](https://doi.org/10.1007/978-3-642-34047-5_18), FSE 2012.
8. M. Brickenstein, A. Dreyer, [PolyBoRi: a framework for Gröbner-basis computations with Boolean polynomials](https://doi.org/10.1016/j.jsc.2008.02.017), J. Symb. Comput. 44, 2009.
9. J. Ding, J. Buchmann, M. S. E. Mohamed, W. S. A. Mohamed, R.-P. Weinmann, MutantXL, SCC 2008.
10. M. Brickenstein, Boolean Gröbner bases: theory, algorithms and applications, PhD thesis, TU Kaiserslautern, 2010.
11. P. Jovanovic, M. Kreuzer, [Algebraic attacks using SAT-solvers](https://doi.org/10.1515/gcc.2010.016), Groups Complex. Cryptol. 2, 2010.
