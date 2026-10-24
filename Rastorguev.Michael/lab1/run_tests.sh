#!/bin/bash

set -e
cd "$(dirname "$0")"

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT

g++ -std=c++17 -Wall -Werror src/implementions/config.cpp tests/test.cpp -o "$TMP_DIR/tests"
"$TMP_DIR/tests"
