#!/usr/bin/bash
set -x
set -e

# cat <<EOF > problem.anf
# x(1)
# x(1)*x(2)
# x(1) + x(2) + x(3) + x(5) + x4
# x(1) + x(2) + x(3) + x(5)*x(4)
# EOF


if [ "$#" -ne 1 ] && [ "$#" -ne 2 ] ; then
  echo "Usage: $0 cryptominisat_binary_location [seed]" >&2
  exit 1
fi

dir="out_fuzz"
mkdir -p $dir
if test -f "$1"; then
    echo "Using CryptoMiniSat binary: $1"
else
    echo "ERROR: CryptoMiniSat not found at '$1'. You must give the 1st parameter as the binary for cryptominisat5"
    exit -1
fi


CRYPTOMINISAT=$1
function one_run {
    r=$1
    echo "Doing random seed $r"
    ./anf_gen.py $r > $dir/problem.anf

    # Solve with simplifications turned off
    ./bosphorus --simplify 0 --anfread $dir/problem.anf --cnfwrite $dir/problem_simple.cnf > $dir/bosph_out
    $CRYPTOMINISAT --maxconfl 40000 --zero-exit-status $dir/problem_simple.cnf --verb 0 > $dir/simple_sol.out
    if grep -q "^s SATISFIABLE$" "$dir/simple_sol.out"; then
        echo "Problem is SAT as per simple solving"
        SATISFIABLE=1 # SomeString was not found
    elif grep -q "^s UNSATISFIABLE$" "$dir/simple_sol.out"; then
        echo "Problem is UNSAT as per simple solving"
        SATISFIABLE=0
    else
        echo "Problem is UNKNOWN as per simple solving"
        SATISFIABLE=99
    fi

    # translate to CNF, solve
    ./bosphorus --anfread $dir/problem.anf --cnfwrite $dir/problem.cnf --solmap $dir/map --solve > $dir/bosph_out

    # check ANF solution
    ./check_solution.py $dir/bosph_out $dir/problem.anf $SATISFIABLE

    # Check that CNF was well-formatted
    ./check_cnf.py $dir/problem.cnf

    # Solve CNF
    $CRYPTOMINISAT --verb 0 --zero-exit-status $dir/problem.cnf > $dir/cnfsol

    # Map CNF solution to ANF
    ./map_solution.py $dir/map $dir/cnfsol > $dir/anfsol

    # Check ANF solution that was mapped
    ./check_solution.py $dir/anfsol $dir/problem.anf $SATISFIABLE

    # Solve CNF, but this time, count the number of solutions
    $CRYPTOMINISAT --zero-exit-status $dir/problem.cnf --maxsol 1025 > $dir/numsol

    # If the number of variables is not too high, then:
    #   brute-force count no. solutions to ANF, and check against the CNF
    ./count_anf_solutions.py $dir/problem.anf $dir/numsol
}

#!/bin/sh
if [ "$#" -ne 2 ]; then
    echo "Doing random seeds"
    for i in {1..10000};
    do
        r=$RANDOM
        one_run $r
    done
else
    echo "Using seed given on command line: $2"
    one_run $2
fi
