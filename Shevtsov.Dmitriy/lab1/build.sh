#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"

mkdir -p build
trap 'rm -rf build' EXIT

for src in $(find src -name '*.cpp'); do
    g++ -std=c++20 -Wall -Werror -Isrc -c "$src" -o "build/$(basename "$src" .cpp).o"
done
g++ build/*.o -o reminder

echo "Built ./reminder"
