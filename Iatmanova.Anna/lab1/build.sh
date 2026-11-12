#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR="build"
OUTPUT="daemon14"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
trap 'rm -rf "$BUILD_DIR"' EXIT

g++ -std=c++17 -Wall -Werror -c src/main.cpp -o "$BUILD_DIR/main.o"
g++ -std=c++17 -Wall -Werror -c src/daemon.cpp -o "$BUILD_DIR/daemon.o"
g++ "$BUILD_DIR/main.o" "$BUILD_DIR/daemon.o" -o "$OUTPUT"

echo "Built ./$OUTPUT"
