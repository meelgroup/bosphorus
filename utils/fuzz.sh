#!/usr/bin/bash
set -x
set -e

# ./anf_gen.py > myanf
cat <<EOF > myanf
x(1)
x(1)*x(2)
x(1) + x(2) + x(3) + x(5) + x4
x(1) + x(2) + x(3) + x(5)*x(4)
EOF

./bosphorus --anfread myanf --cnfwrite mycnf --solmap map
/home/soos/development/sat_solvers/cryptominisat/build/cryptominisat5 --verb 0 --zero-exit-status mycnf > cnfsol
./map_solution.py map cnfsol > anfsol
./check_solution.py anfsol myanf
