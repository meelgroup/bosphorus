Bosphorus is an ANF simplification and solving tool. It takes as input an ANF over GF(2) and can simplify it and also solve it. As a plus, it can also take in a CNF, convert it to ANF, simplify it, then either give a (hopefully simplified) CNF, an ANF, or can run a SAT solver directly and solve it. It uses the following systems: CryptoMiniSat, espresso, M4RI and PolyBoRi.

The system was published in DATE 2019, see PDF here.

# Usage
Suppose we have a system of 2 equations:
1. x1 + x2 + x3 = 0
2. x1 \* x2 + x2 \* x3 + 1 = 0

Write an ANF file `readme.anf` as follows:
```
x1 + x2 + x3
x1*x2 + x2*x3 + 1
```

Running `./bosphorus --anfread readme.anf --anfwrite readme.anf.out --custom --elsimp` will apply only the ElimLin simplification (ignoring other processes that the tool offers) to `readme.anf` and write it out to `readme.anf.out`. `readme.anf.out` will then ßcontain the following:
```
c Executed arguments: --anfread readme.anf --anfwrite readme.anf.out --custom --elsimp
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
Explanation of simplification done:
* The first linear polynomial rearranged to `x1 = x2 + x3` to eliminate x1 from the other equations (in this case, there is only one other equation)
* The second polynomial becomes `(x2 + x3) * x2 + x2 * x3 + 1 = 0`, which simplifies to `x2 + 1 = 0`
* Then, further simplification by substituting `x2 + 1 = 0` yields `x1 + x3 + 1 = 0`


The output of `readme.anf.out` tells us that
* x2 = 1
* x1 != x3

i.e. There are 2 solutions:
* (x1, x2, x3) = (1,1,0)
* (x1, x2, x3) = (0,1,1)

See `./bosphorus -h` for the full list of options.

# Installation

### Docker
TODO

### m4ri
From https://bitbucket.org/malb/m4ri/downloads/, download `m4ri-20140914.tar.gz`

```
tar -xf m4ri-20140914.tar.gz
cd m4ri-20140914
./configure
make
sudo make install
```

### PolyBoRi
* We currrently link against BRiAl actually
From https://sourceforge.net/projects/polybori/files/polybori/0.8.3/, download `polybori-0.8.3.tar.gz`
```
tar -xf polybori-0.8.3.tar.gz
cd polybori-0.8.3
sudo scons devel-install
```

### Cryptominisat
```
git clone https://github.com/msoos/cryptominisat.git
cd cryptominisat
mkdir build
cd build
cmake ..
make -j4
sudo make install
```
Note (For MacOS): If you encounter `cryptominisat.h:30:10: fatal error: 'atomic' file not found` in `#include <atomic>` during compilation, you may need to use `CFLAGS='-stdlib=libc++' make` instead of just `make`.

### This tool, once everything above is installed
```
git clone https://github.com/cxjdavin/indra.git
cd indra
mkdir build
cd build
cmake ..
make -j4
```
Use `cmake -DSTATICCOMPILE=ON ..` instead if you wish to compile statically. You may now run using the executable `indra` in the `build` directory.

### Testing
Must have [LLVM lit](https://github.com/llvm-mirror/llvm/tree/master/utils/lit) and [stp OutputCheck](https://github.com/stp/OutputCheck). Please install with:
```
pip install lit
pip install OutputCheck
```
Run test suite via `lit indra/tests`

# Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not run `indra` on instances with over a million variables.
