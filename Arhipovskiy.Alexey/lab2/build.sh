#!/bin/sh
# Сборка всех host_* и client_* через CMake
#
# Использование:  ./build.sh  или  sh build.sh  (то же делает make)

set -eu

cd "$(dirname "$0")"

for tool in cmake make; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Ошибка: не найден $tool, нужные пакеты перечислены в README.md (раздел «Установка и сборка»)" >&2
        exit 1
    fi
done

BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT

# CMAKE_ARGS: param cmake, for example CMAKE_ARGS="-DCMAKE_CXX_COMPILER=clang++"
# shellcheck disable=SC2086
if ! cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS:-} >/dev/null; then
    echo "Ошибка: CMake не настроил сборку, нужны компилятор C++17 и Qt 5 или 6 (см. README.md)" >&2
    exit 1
fi
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 2)"

rm -f host_* client_*
for program in "$BUILD_DIR"/host_* "$BUILD_DIR"/client_*; do
    if [ -f "$program" ] && [ -x "$program" ]; then
        cp "$program" .
    fi
done
echo "Готово:" host_* client_*
