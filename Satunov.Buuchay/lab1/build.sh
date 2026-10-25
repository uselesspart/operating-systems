#!/usr/bin/env bash
set -euo pipefail

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -Wall -Wextra -Werror -pedantic"
BUILD_DIR="build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

"$CXX" $CXXFLAGS \
    -Iinclude \
    src/config.cpp \
    tests/unit/test_config.cpp \
    -o "$BUILD_DIR/test_config"

"$BUILD_DIR/test_config"

"$CXX" $CXXFLAGS \
    -Iinclude \
    src/config.cpp \
    src/main.cpp \
    -o daemon