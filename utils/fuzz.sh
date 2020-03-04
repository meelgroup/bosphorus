#!/usr/bin/bash
set -x
set -e

# cat <<EOF > problem.anf
# x(1)
# x(1)*x(2)
# x(1) + x(2) + x(3) + x(5) + x4
# x(1) + x(2) + x(3) + x(5)*x(4)
# EOF

dir="out_fuzz"
mkdir -p $dir

function one_run {
    r=$1
    echo "Doing random seed $r"
    ./anf_gen.py $r > $dir/problem.anf
    ./bosphorus --anfread $dir/problem.anf --cnfwrite $dir/problem.cnf --solmap $dir/map > $dir/bosph_out
    ./check_cnf.py $dir/problem.cnf
    /home/soos/development/sat_solvers/cryptominisat/build/cryptominisat5 --verb 0 --zero-exit-status $dir/problem.cnf > $dir/cnfsol
    ./map_solution.py $dir/map $dir/cnfsol > $dir/anfsol
    ./check_solution.py $dir/anfsol $dir/problem.anf
    /home/soos/development/sat_solvers/cryptominisat/build/cryptominisat5 --zero-exit-status $dir/problem.cnf --maxsol 1025 > $dir/numsol
    ./count_anf_solutions.py $dir/problem.anf $dir/numsol
}

#!/bin/sh
if [ "$#" -ne 1 ]; then
    echo "Doing random seeds"
    for i in {1..10000};
    do
        r=$RANDOM
        one_run $r
    done
else
    echo "Using seed given on command line: $1"
    one_run $1
fi
