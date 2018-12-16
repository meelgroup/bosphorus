Bosphorus is an ANF simplification and solving tool. It takes as input an ANF over GF(2) and can simplify it and also solve it. As a plus, it can also take in a CNF, convert it to ANF, simplify it, then either give a (hopefully simplified) CNF, an ANF, or can run a SAT solver directly and solve it. It uses the following systems: CryptoMiniSat, espresso, M4RI and PolyBoRi.

If you use Bosphorus, please cite our [paper](https://www.comp.nus.edu.sg/~meel/Papers/date-cscm19.pdf) ([bibtex](https://www.comp.nus.edu.sg/~meel/bib/CSCM19.bib)) published at DATE 2019.

# Usage
The sytem can be used to:
* Simplify ANF problems
* Solve ANF problems
* Simplify CNF problems
* Solve CNF problems

When simplifying or solving CNF problems, the CNF is (extremely) naively translated to ANF, then simplifications are applied, and a sophisticated system then translates the ANF back to CNF. This CNF can then be optinally solved.

## ANF simplification and solving
Suppose we have a system of 2 equations:
1. x1 ⊕ x2 ⊕ x3 = 0
2. x1 \* x2 ⊕ x2 \* x3 + 1 = 0

Write the ANF file `myeqs.anf`:
```
echo "x1 + x2 + x3
x1*x2 + x2*x3 + 1" > myeqs.anf
./bosphorus --anfread test.anf --anfwrite out.anf -s --solvewrite solution
```

The simplified ANF is in `out.anf`

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

A solution to the problem is in `solution`:
```
cat solution
v -0 1 2 -3
```
Which solves satisfies the set of equations.


Explanation of simplifications performed:
* The first linear polynomial rearranged to `x1 = x2 + x3` to eliminate x1 from the other equations (in this case, there is only one other equation)
* The second polynomial becomes `(x2 + x3) * x2 + x2 * x3 + 1 = 0`, which simplifies to `x2 + 1 = 0`
* Then, further simplification by substituting `x2 + 1 = 0` yields `x1 + x3 + 1 = 0`


See `./bosphorus -h` for the full list of options.

## CNF simplification and solving

Blah


# Building, Testing, Installing
You must install M4RI, BriAl, and CryptoMiniSat to use compile Bosphorus. Below, we explain how to compile them all.


### Install base dependencies

```
sudo apt-get install build-essential cmake zlib1g-dev libboost-program-options-dev libm4ri-dev
```

### Install BRiAl
```
git clone --depth 1 https://github.com/BRiAl/BRiAl
cd BRiAl
aclocal
autoheader
libtoolize --copy
automake --copy --add-missing
automake
autoconf
./configure
make -j4
sudo make install
```

### Install CryptoMiniSat
```
git clone --depth 1 https://github.com/msoos/cryptominisat.git
cd cryptominisat
mkdir build
cd build
cmake ..
make -j4
sudo make install
```
Note (For MacOS): If you encounter `cryptominisat.h:30:10: fatal error: 'atomic' file not found` in `#include <atomic>` during compilation, you may need to use `CFLAGS='-stdlib=libc++' make` instead of just `make`.

### Build Bosphorus (this tool)
```
git clone --depth 1 https://github.com/meelgroup/bosphorus
cd bosphorus
mkdir build
cd build
cmake ..
make -j4
./bosphorus -h
```

# Testing
Must have [LLVM lit](https://github.com/llvm-mirror/llvm/tree/master/utils/lit) and [stp OutputCheck](https://github.com/stp/OutputCheck). Please install with:
```
pip install lit
pip install OutputCheck
```
Run test suite via `lit bosphorus/tests`

# Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not run `bosphorus` on instances with over a million variables.

# Parameters used in the paper
For the paper, the following parameters are used for learning
```
bosphorus --anfread a.anf --cnfwrite a-learnt.cnf --stoponsolution --learnsolution --maxtime=1000
```

for just conversion, use
```
bosphorus --anfread a.anf --cnfwrite a-convert.cnf --stoponsolution --learnsolution --maxtime=0
``` 

For cnfs, use `--cnfread a.cnf' instead of '--anfread a.anf`
