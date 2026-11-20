#!/usr/bin/env bash

set -e

cd "$(dirname "$0")"

rm -rf build

cmake -S . -B build
cmake --build build -j"$(nproc)"

echo "Сборка завершена."