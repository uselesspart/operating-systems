#!/bin/sh
set -eu

cd "$(dirname "$0")/.."

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"
(cd "$BUILD_DIR" && ctest --output-on-failure)
