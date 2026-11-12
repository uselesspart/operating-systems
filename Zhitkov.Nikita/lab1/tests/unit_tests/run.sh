#!/bin/bash
# Сборка и запуск unit-тестов.
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD_DIR="$ROOT/build_tests"
mkdir -p "$BUILD_DIR"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -Wall -Werror -O2 -I$ROOT/include -I$HERE/include"

$CXX $CXXFLAGS \
    "$HERE/src/main.cpp" \
    "$HERE/src/test_config.cpp" \
    "$HERE/src/test_events.cpp" \
    "$ROOT/src/Config.cpp" \
    "$ROOT/src/Event.cpp" \
    -o "$BUILD_DIR/unit_tests"

"$BUILD_DIR/unit_tests"

rm -rf "$BUILD_DIR"
echo "Unit tests passed"