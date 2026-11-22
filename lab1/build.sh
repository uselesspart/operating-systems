#!/usr/bin/env bash
# Сборка демона: ./build.sh  ->  ./daemon (промежуточные файлы удаляются)
set -euo pipefail
cd "$(dirname "$0")"

OBJ="$(mktemp -d)"
trap 'rm -rf "$OBJ"' EXIT

for src in src/*.cpp; do
    g++ -std=c++17 -Wall -Werror -c "$src" -o "$OBJ/$(basename "${src%.cpp}").o"
done
g++ "$OBJ"/*.o -o daemon
echo "Build OK: ./daemon"
