#!/bin/bash
set -e

echo "==> Building Unit-tests..."
rm -rf build_tests
cmake -S . -B build_tests -DBUILD_TESTING=ON
cmake --build build_tests -j$(nproc)

echo "==> Running Unit-tests..."
cd build_tests
ctest --output-on-failure