#!/bin/bash
# Сборка демона: компилирует все исходники из src/ в один исполняемый файл ./reminderd
# и удаляет за собой промежуточные (объектные) файлы.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

OBJ_DIR="$(mktemp -d)"
trap 'rm -rf "$OBJ_DIR"' EXIT   # промежуточные файлы удаляются всегда, даже при ошибке

CXXFLAGS=(-std=c++17 -O2 -Wall -Werror)

for src in src/*.cpp; do
    g++ "${CXXFLAGS[@]}" -c "$src" -o "$OBJ_DIR/$(basename "${src%.cpp}").o"
done
g++ "$OBJ_DIR"/*.o -o reminderd

echo "Сборка завершена: $(pwd)/reminderd"
