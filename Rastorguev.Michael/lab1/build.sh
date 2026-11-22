#!/bin/bash

set -e
cd "$(dirname "$0")"

OBJ_DIR=$(mktemp -d)
trap 'rm -rf "$OBJ_DIR"' EXIT

for src in src/main.cpp src/implementions/*.cpp; do
    obj="$OBJ_DIR/$(echo "${src%.cpp}" | tr '/' '_').o"
    g++ -std=c++17 -Wall -Werror -c "$src" -o "$obj"
done

g++ "$OBJ_DIR"/*.o -o schedule_daemon
echo "Готово: ./schedule_daemon"
