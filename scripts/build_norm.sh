#!/usr/bin/bash

rm -rf CMake* cm* lib* bosph* Testing* tests* include tests* CPack*
cmake -DENABLE_TESTING=ON \
    -Dcadical_DIR=../../cadical/build \
    -Dcadiback_DIR=../../cadiback/build \
    -Dcryptominisat5_DIR=../../cryptominisat/build \
    ..
make -j
make test
