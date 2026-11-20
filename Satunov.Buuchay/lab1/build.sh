#!/usr/bin/env bash
set -euo pipefail

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++20 -Wall -Wextra -Werror -pedantic -pthread"

BUILD_DIR="build"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

trap 'rm -rf -- "$BUILD_DIR"' EXIT

"$CXX" $CXXFLAGS \
    -Iinclude \
    src/config.cpp \
    tests/unit/test_config.cpp \
    -o "$BUILD_DIR/test_config"

"$BUILD_DIR/test_config"

"$CXX" $CXXFLAGS \
    -Iinclude \
    src/cleaner.cpp \
    tests/unit/test_cleaner.cpp \
    -o "$BUILD_DIR/test_cleaner"

"$BUILD_DIR/test_cleaner"

"$CXX" $CXXFLAGS \
    -Iinclude \
    src/config.cpp \
    src/cleaner.cpp \
    src/daemon.cpp \
    src/main.cpp \
    -o "$BUILD_DIR/daemon"

mv -fT -- "$BUILD_DIR/daemon" ./daemon
