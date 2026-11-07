#!/bin/bash
# Сборка проекта через CMake. Все промежуточные файлы CMake создаются во временном каталоге
# и удаляются при выходе (в том числе при ошибке); в проекте остаются только исполняемые файлы.
#
# Использование: ./build.sh [--no-gui] [--out КАТАЛОГ]
#   --no-gui      собрать хост без Qt (консольный режим)
#   --out КАТАЛОГ положить исполняемые файлы в КАТАЛОГ (по умолчанию ./bin)
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

NO_GUI=OFF
OUT="$(pwd)/bin"
while [ $# -gt 0 ]; do
    case "$1" in
        --no-gui) NO_GUI=ON ;;
        --out) shift; OUT="$(mkdir -p "$1" && cd "$1" && pwd)" ;;
        *) echo "Неизвестный параметр: $1" >&2; echo "Использование: $0 [--no-gui] [--out КАТАЛОГ]" >&2; exit 2 ;;
    esac
    shift
done

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DNO_GUI="$NO_GUI" -DOUTPUT_DIR="$OUT"
cmake --build "$BUILD_DIR" -- -j"$(nproc)"

echo
echo "Сборка завершена. Исполняемые файлы: $OUT"
ls "$OUT" | grep -E '^(host|client)_' | sed 's/^/  /'
