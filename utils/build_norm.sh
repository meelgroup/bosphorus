#!/usr/bin/bash

rm -rf CMake* cm* lib* bosph* Testing* tests* include tests* CPack*
cmake -DENABLE_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ..
make -j24
