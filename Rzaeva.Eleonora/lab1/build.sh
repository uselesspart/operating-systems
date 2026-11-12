#!/usr/bin/env bash
#
# Сборка демона в один исполняемый файл ./daemon.
# Флаги -Wall -Werror, промежуточные объектные файлы удаляются автоматически.
#
set -euo pipefail
cd "$(dirname "$0")"

CXX="${CXX:-g++}"
STD="${STD:-c++17}"
FLAGS=(-std="$STD" -Wall -Werror -O2)

objdir="$(mktemp -d)"
trap 'rm -rf "$objdir"' EXIT   # за собой убираем

objects=()
for src in src/*.cpp; do
    obj="$objdir/$(basename "${src%.cpp}").o"
    echo "compile $src"
    "$CXX" "${FLAGS[@]}" -c "$src" -o "$obj"
    objects+=("$obj")
done

echo "link daemon"
"$CXX" "${FLAGS[@]}" "${objects[@]}" -o daemon

echo "Built ./daemon successfully"
