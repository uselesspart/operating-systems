#!/bin/bash
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
BUILD="$ROOT/build_tests"
mkdir -p "$BUILD"

g++ -std=c++17 -Wall -Werror -O2 \
    -I"$ROOT/include" -I"$HERE/include" \
    "$HERE/src/main.cpp" \
    "$HERE/src/test_scenario.cpp" \
    "$HERE/src/test_protocol.cpp" \
    "$HERE/src/test_sems.cpp" \
    "$ROOT/src/Scenario.cpp" \
    -o "$BUILD/unit_tests" -pthread

"$BUILD/unit_tests"

rm -rf "$BUILD"