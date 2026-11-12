#!/usr/bin/env bash

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CXX="${CXX:-g++}"
CXXFLAGS=(-std=c++17 -Wall -Werror -g)
OUTPUT="$SCRIPT_DIR/daemon_lab1"
NEW_OUTPUT="$OUTPUT.new"

if ! command -v "$CXX" >/dev/null 2>&1; then
    echo "[build] Компилятор '$CXX' не найден" >&2
    exit 1
fi

shopt -s nullglob
SOURCES=("$SCRIPT_DIR"/src/*.cpp)
shopt -u nullglob

if [ "${#SOURCES[@]}" -eq 0 ]; then
    echo "[build] В $SCRIPT_DIR/src нет .cpp файлов" >&2
    exit 1
fi

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/daemon_lab1_build.XXXXXX")"

cleanup() {
    rm -rf -- "$BUILD_DIR"
    rm -f -- "$NEW_OUTPUT"
}

on_error() {
    echo "[build] ОШИБКА: сборка не удалась" >&2
    if [ -e "$OUTPUT" ]; then
        echo "[build] Предыдущая версия сохранена: $OUTPUT" >&2
    fi
}

trap cleanup EXIT
trap on_error ERR

OBJECTS=()

for src in "${SOURCES[@]}"; do
    name="$(basename "$src" .cpp)"
    echo "[build] Компиляция src/$name.cpp"
    "$CXX" "${CXXFLAGS[@]}" -I"$SCRIPT_DIR/include" -c "$src" -o "$BUILD_DIR/$name.o"
    OBJECTS+=("$BUILD_DIR/$name.o")
done

echo "[build] Линковка daemon_lab1"
"$CXX" "${CXXFLAGS[@]}" "${OBJECTS[@]}" -o "$NEW_OUTPUT"

mv -f -- "$NEW_OUTPUT" "$OUTPUT"

echo "[build] Готово: $OUTPUT"