#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

mkdir -p build
trap 'rm -rf build' EXIT

g++ -std=c++20 -Wall -Werror -Isrc tests/tests.cpp src/parser/parser.cpp -o build/tests
./build/tests
