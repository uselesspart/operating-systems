#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

trap 'rm -rf build' EXIT

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j "$(nproc)"

echo "Built:" host_* tests_*
