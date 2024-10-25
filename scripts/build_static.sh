#!/usr/bin/bash

rm -rf CMake* cm* lib* bosph* Testing* tests* include tests* CPack*
cmake -DENABLE_TESTING=ON -DSTATICCOMPILE=ON ..
make -j6 VERBOSE=1
make test
