#!/bin/bash
set -e

cd "$(dirname "$0")" 

echo "Building main project"
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j"$(nproc)"

cp build/host_* build/client_* . 2>/dev/null || true

echo "Build complete"