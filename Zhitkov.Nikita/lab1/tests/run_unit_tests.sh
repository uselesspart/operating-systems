#!/bin/bash
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build_tests"
mkdir -p "$BUILD_DIR"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -Wall -Werror -O2 -I$ROOT/include -I$ROOT/tests/include"

$CXX $CXXFLAGS \
    "$ROOT/tests/src/main.cpp" \
    "$ROOT/tests/src/test_config.cpp" \
    "$ROOT/tests/src/test_events.cpp" \
    "$ROOT/src/Config.cpp" \
    "$ROOT/src/Event.cpp" \
    -o "$BUILD_DIR/unit_tests"

"$BUILD_DIR/unit_tests"

rm -rf "$BUILD_DIR"
echo "Unit tests passed"