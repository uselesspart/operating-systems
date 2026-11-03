#!/bin/bash
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD="$ROOT/build_tests"
mkdir -p "$BUILD"

for t in mq fifo sock; do
    g++ -std=c++17 -Wall -Werror -O2 -I"$ROOT/include" \
        "$HERE/test_conn_${t}.cpp" \
        "$ROOT/src/ClientLoop.cpp" \
        "$ROOT/src/conn_${t}.cpp" \
        -o "$BUILD/test_conn_${t}" -pthread -lrt
    "$BUILD/test_conn_${t}"
done

rm -rf "$BUILD"
echo "Integration tests PASSED"