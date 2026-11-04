#!/bin/bash
set -e
rm -rf build
mkdir build && cd build
cmake ..
make -j$(nproc)
echo "Built: host_mmap, host_shm, host_pipe, client_mmap, client_shm, client_pipe"
# clean intermediates after build (optional keep binaries)
# rm -rf CMakeFiles CMakeCache.txt cmake_install.cmake Makefile
