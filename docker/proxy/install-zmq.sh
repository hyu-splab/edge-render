#!/bin/sh
git clone https://github.com/zeromq/cppzmq.git
apt install libzmq3-dev -y
cd cppzmq
mkdir build
cd build
cmake ..

CPPZMQ_BUILD_TESTS:BOOL=OFF
sed -i 's/CPPZMQ_BUILD_TESTS:BOOL=ON/CPPZMQ_BUILD_TESTS:BOOL=OFF/g' CMakeCache.txt
make install -j4