#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(
    cd "$(dirname "${BASH_SOURCE[0]}")"
    pwd
)"

PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$(mktemp -d)"

trap 'rm -rf -- "$BUILD_DIR"' EXIT

echo "Building configuration tests..."

g++ \
    -std=c++17 \
    -Wall \
    -Werror \
    -I"$PROJECT_DIR/include" \
    "$SCRIPT_DIR/ConfigLoaderTests.cpp" \
    "$PROJECT_DIR/src/ConfigLoader.cpp" \
    -o "$BUILD_DIR/config_tests"

"$BUILD_DIR/config_tests"
