#!/usr/bin/bash
set -x
set -e

# cat <<EOF > myanf
# x(1)
# x(1)*x(2)
# x(1) + x(2) + x(3) + x(5) + x4
# x(1) + x(2) + x(3) + x(5)*x(4)
# EOF

function one_run {
    r=$1
    echo "Doing random seed $r"
    ./anf_gen.py $r > out_fuzz/myanf
    ./bosphorus --anfread myanf --cnfwrite out_fuzz/mycnf --solmap out_fuzz/map > out_fuzz/bosph_out
    ./check_cnf.py out_fuzz/mycnf
    /home/soos/development/sat_solvers/cryptominisat/build/cryptominisat5 --verb 0 --zero-exit-status out_fuzz/mycnf > out_fuzz/cnfsol
    ./map_solution.py out_fuzz/map cnfsol > out_fuzz/anfsol
    ./check_solution.py out_fuzz/anfsol out_fuzz/myanf
}

#!/bin/sh
if [ "$#" -ne 1 ]; then
    echo "Doing random seeds"
    for i in {1..1000};
    do
        r=$RANDOM
        one_run $r
    done
else
    echo "Using seed given on command line: $1"
    one_run $1
fi
