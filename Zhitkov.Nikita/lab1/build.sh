#!/bin/bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/build"
BIN="$ROOT/reminder_daemon"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

CXX="${CXX:-g++}"
CXXFLAGS="-std=c++17 -Wall -Werror -O2 -I$ROOT/include"

$CXX $CXXFLAGS -c "$ROOT/src/Config.cpp"  -o "$BUILD_DIR/Config.o"
$CXX $CXXFLAGS -c "$ROOT/src/Event.cpp"   -o "$BUILD_DIR/Event.o"
$CXX $CXXFLAGS -c "$ROOT/src/Daemon.cpp"  -o "$BUILD_DIR/Daemon.o"
$CXX $CXXFLAGS -c "$ROOT/src/main.cpp"    -o "$BUILD_DIR/main.o"

$CXX "$BUILD_DIR/Config.o" "$BUILD_DIR/Event.o" \
     "$BUILD_DIR/Daemon.o" "$BUILD_DIR/main.o" \
     -o "$BIN"

# Чистим промежуточные файлы
rm -rf "$BUILD_DIR"

echo "Built: $BIN"