#!/bin/bash
set -e
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="$ROOT/build"

rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"
cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null
make -j"$(nproc)"

cp host_mq host_fifo host_sock "$ROOT/"
cd "$ROOT"
rm -rf "$BUILD"

echo "Built:"
ls -1 host_*