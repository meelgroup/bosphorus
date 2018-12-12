#!/bin/bash

# Installing librariesi
sudo apt-get update
sudo apt-get install build-essential cmake libzip-dev libboost-program-options-dev libm4ri-dev libsqlite3-dev libbrial-dev

# Installing m4ri
tar -xf m4ri-20140914.tar.gz
cd m4ri-20140914
./configure
make
sudo make install
cd ..

# Installing cryptominisat
git clone https://github.com/msoos/cryptominisat.git
cd cryptominisat
mkdir build
cd build
cmake ..
# CFLAGS='-stdlib=libc++' make -j4
make -j4
sudo make install
cd ../..

mkdir build
cd build
# cmake -DSTATICCOMPILE=ON ..
cmake ..
make -j4
cd ..
