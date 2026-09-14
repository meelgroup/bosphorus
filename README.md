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

Simplify it, write the simplified ANF and CNF, solve and write the solution
(a `.anf` input is read as ANF, a `.cnf` input as CNF):
```
$ ./bosphorus test.anf --anfwrite out.anf --cnfwrite out.cnf --solvewrite solution
```

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


| rule | what it does | switch |
|---|---|---|
| `anf-prop` | propagates units, (anti-)equivalences and `m+1` (all variables of `m` true) | always on |
| `lin-gauss` | Gaussian elimination among the linear equations: drops redundant ones, shortens others, feeds units and equivalences to `anf-prop` | `--lingauss` |
| `binom-red` | reduces every equation modulo the monomial and binomial equations (`x*y = 0`, `x*y + x = 0`, definitions `x*y + z = 0`); degrees never grow | `--binomred`, `--binomredlen` |
| `poly-shorten` | replaces `p` by `p + f` when that is shorter (shortens XORs, re-uses definitions) | `--shorten` |
| `lit-probe` | partial evaluation of small equations: forced literals, equivalences, binary implications and their SCCs | `--probe`, `--probevars` |
| `fac-canon` | canonical linear factors of products modulo the linear span (off) | `--faccanon` |
| `fac-res` | resolution between products sharing a linear factor (off) | `--facres`, `--facresmax` |
| `gb-cone` | Gröbner bases of cones of small equations; short basis members are added. Off for large systems, always on for systems of at most `--gbwholevars` variables | `--gb`, `--gbwholevars`, `--gbengine`, `--gb*` |
| `xl` | eXtended Linearization on equations of at most `--xlmaxlen` terms | `--xl`, `--xldeg`, `--xlsample`, `--xlmaxlen` |
| `elimlin` | ElimLin: elimination and substitution of linear equations, iterated | `--el`, `--elsample` |
| `sat-simp` | bounded CryptoMiniSat run; imports the units, equivalences and XORs it finds | `--sat`, `--satinc`, `--satlim` |


#### The Gröbner engines

Complete bases come from Bosphorus's own matrix-F4 engine (`--gbengine 1`,
M4RI over squarefree 64-bit monomials, deterministic budgets `--gbsteps`
and `--gbmaxcells`); `--gbengine 0` uses BRiAl's `symmGB_F2` and
`--gbengine 2` a Matrix-F5 variant that is faster on random quadratic
systems and slower on structured ones.

### ANF-to-CNF conversion strategies

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
variables in the CNF, so solution counts over it agree.

### Multivariate quadratic (MQ) and HFE systems

Post-quantum multivariate schemes reduce to quadratic systems over GF(2).
Bosphorus reads [Fukuoka MQ challenge](https://www.mqchallenge.org/) files
and Magma polynomial lists such as the HFE systems of
`magma.maths.usyd.edu.au/users/allan/gb` through converters in `utils/`,
and can generate random MQ systems with a planted solution. A system with
few enough variables is solved by its Gröbner basis alone, without any
SAT solving.

Random MQ systems with m = 2n and a planted solution, one core, 200 s
limit for the SAT solver:

| n | CryptoMiniSat on Bosphorus's CNF | BRiAl `symmGB_F2` | Bosphorus F4 | Bosphorus F5 |
|---|---|---|---|---|
| 16 | 0.7 s | 0.3 s | 0.2 s | 0.1 s |
| 20 | 1.4 s | 1.0 s | 0.7 s | 0.5 s |
| 24 | timeout | 15 s | 2.8 s | 1.1 s |
| 28 | timeout | 196 s | 187 s | |

HFE systems (secret degree 96) from Allan Steel's Magma page, with the
solutions Magma found in 2004:

| system | BRiAl `symmGB_F2` | Bosphorus F4 |
|---|---|---|
| HFE25 | 166 s | 52 s |
| HFE30 | 1329 s | 131 s |
| HFE35 | no answer in 2 h | 167 s (3.9 GB) |

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

## Counting solutions of an ANF

When there are too many solutions to list (say 2**40), count them on the
CNF: a projection set in the ANF (`c p show x1 x2 ... END`) is written into
the CNF as `c p show var1 var2 ... varn 0`, which counters such as
[ApproxMC](https://github.com/meelgroup/approxmc) understand:

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
./bosphorus test.anf --cnfwrite out.cnf
./cryptominisat --maxsol 100000 out.cnf
[...]
c Number of solutions found until now:    16384
s UNSATISFIABLE
```

## CNF simplification

This is **EXPERIMENTAL**: do not rely on its correctness. A CNF input is
naively translated to ANF, simplified, and translated back to CNF (or
solved).

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
$ ./bosphorus test.cnf --anfwrite out2.anf
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

ElimLin recovered the XOR `x(1) + x(2) + x(3)` from the four equations that
encode the first four clauses (the second clause weakens to `2 -3 4 0`, and
the four clauses then encode that XOR).

## Mapping solutions from CNF to ANF

Let's take a simple ANF:

```
$ cat test.anf
x(1) + x2 + x3
x1*x2 + x2*x3 + 1
```

Simplify it into CNF with a solution map:

```
./bosphorus test.anf --cnfwrite test.cnf --solmap solution_map
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
