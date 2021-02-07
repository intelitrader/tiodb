@echo off
mkdir build && cd build
cmake -T v141 -A x64 -Dgtest_force_shared_crt=ON ..
