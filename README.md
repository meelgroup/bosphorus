# About

TO DO
1. Update this README.md

Historic note:
1. Bophorus is originally, an ANF simplification tool forked from https://github.com/msoos/anfconv.
2. Bophorus was originally named Indra.

# Installation

### One-shot deployment
- Use `m4ri-20140914.tar.gz` and `polybori-0.8.3.tar.gz` that we have downloaded before-hand and uploaded into this repository
- Use the bash script `setup.sh` which runs all commands below in sequential order (Includes `sudo` commands)
- [**Strongly recommended**] Take a look at `setup.sh` and modify accordingly before executing

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

### Bosphorus (This tool!)
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

### What is printed by different verbosity levels
(Note: Level N also prints everything from Level 0 to Level N-1)

###### Level 0
Error messages
###### Level 1 (Default)
Basic ANF/CNF statistics
###### Level 2
Some simplification information (e.g. Summary of each simplification step)
###### Level 3
More detailed simplification information (e.g. Prints matrix sizes)
###### Level 4
Even more detailed simplification information (e.g. Prints matrix before and after Gauss Jordan Elimination)
###### Level 5
Warning! Data dump from this point onwards

# Known issues
- PolyBoRi cannot handle ring of sizes over approx 1 million (1048574). Do not run `indra` on instances with over a million variables.
