#!/usr/bin/bash

rm -rf CMake* cm* lib* bosph* Testing* tests* include tests* CPack*
cmake -DENABLE_TESTING=ON ..
make -j24
