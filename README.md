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

### Building from source

The build uses CMake and automatically fetches and compiles CryptoMiniSat (and
in turn its own dependencies), so the only C++ dependencies you need to provide
are Boost, zlib, GMP, m4ri and BRiAl. Install the system packages:

```shell
# Debian/Ubuntu
sudo apt-get install build-essential cmake pkg-config git zlib1g-dev libgmp-dev \
                     libboost-program-options-dev libboost-test-dev

# macOS (brew)
brew install cmake pkg-config automake libtool boost gmp
```

Neither [m4ri](https://github.com/malb/m4ri) nor
[BRiAl](https://github.com/BRiAl/BRiAl) is packaged on current Ubuntu or in
Homebrew, so install them from their release tarballs. On Debian/Ubuntu:
```shell
curl -sSfL -O https://github.com/malb/m4ri/releases/download/20250128/m4ri-20250128.tar.gz
tar xf m4ri-20250128.tar.gz && cd m4ri-20250128
./configure --enable-static --enable-shared --with-pic --disable-png
make -j$(nproc) && sudo make install && cd ..

curl -sSfL -O https://github.com/BRiAl/BRiAl/releases/download/1.2.12/brial-1.2.12.tar.bz2
tar xf brial-1.2.12.tar.bz2 && cd brial-1.2.12
./configure --enable-static --enable-shared --with-pic CXXFLAGS="-O2 -g -std=gnu++17"
make -j$(nproc) && sudo make install && cd ..
```

On macOS, BRiAl needs Homebrew's Boost pointed at explicitly:
```shell
curl -sSfL -O https://github.com/malb/m4ri/releases/download/20250128/m4ri-20250128.tar.gz
tar xf m4ri-20250128.tar.gz && cd m4ri-20250128
./configure --enable-static --enable-shared --with-pic --disable-png
make -j8 && sudo make install && cd ..

curl -sSfL -O https://github.com/BRiAl/BRiAl/releases/download/1.2.12/brial-1.2.12.tar.bz2
tar xf brial-1.2.12.tar.bz2 && cd brial-1.2.12
./configure --enable-static --enable-shared --with-pic \
    --with-boost=$(brew --prefix boost) \
    CXXFLAGS="-O2 -g -std=gnu++17 -I$(brew --prefix boost)/include" \
    LDFLAGS="-L$(brew --prefix boost)/lib"
make -j8 && sudo make install && cd ..
```

BRiAl 1.2.12 does not compile as C++20, hence the pinned `-std=gnu++17`.

Then build Bosphorus:
```shell
git clone --recurse-submodules https://github.com/meelgroup/bosphorus
cd bosphorus
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

For a fully static binary (no shared-library dependencies at runtime), add
`-DSTATICCOMPILE=ON`. On macOS this additionally needs a static zlib, which the
SDK does not ship -- build [zlib](https://github.com/madler/zlib) with
`./configure --static` first.

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

## Fuzzing
The tool comes with a built-in ANF fuzzer. To use, install
[cryptominisat](https://github.com/msoos/cryptominisat), then run:

```
git clone --depth 1 https://github.com/meelgroup/bosphorus
cd bosphorus
mkdir build
cd build
ln -s ../utils/* .
./build_normal.sh
./fuzz.sh /usr/bin/cryptominisat5
```

Where the argument to `fuzz.sh` must be the location of the cryptominisat5 binary.

## Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not
  run `bosphorus` on instances with over a million variables.
