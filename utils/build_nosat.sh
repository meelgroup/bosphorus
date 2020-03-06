#!/usr/bin/bash

rm -rf lib bosphorus src espresso CMake* bosph* Testing* tests* include tests* CPack*
cmake -DENABLE_TESTING=ON -DUSE_CMS=OFF ..
make -j6 VERBOSE=1
