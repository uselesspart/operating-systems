#!/usr/bin/env bash

set -e
cd "$(dirname "$0")"

rm -rf build
for f in host_* client_*; do
    [ -f "$f" ] && [ -x "$f" ] && rm -f "$f"
done

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -- -j"$(nproc)"

if [ "$1" != "--keep" ]; then
    rm -rf build
fi
