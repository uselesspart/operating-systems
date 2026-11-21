#!/bin/sh

set -eu

cd "$(dirname "$0")"

for tool in cmake make; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "build.sh: не найден $tool (Debian/Ubuntu: sudo apt install cmake make g++ qt6-base-dev)" >&2
        exit 1
    fi
done

WORK=$(mktemp -d "${TMPDIR:-/tmp}/lab2-build.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if ! cmake -S . -B "$WORK" -DCMAKE_BUILD_TYPE=Release ${CMAKE_ARGS:-} >"$WORK/configure.log" 2>&1; then
    cat "$WORK/configure.log" >&2
    echo "build.sh: cmake не смог настроить сборку, нужен компилятор C++17 и Qt 5 или Qt 6 (см. README.md)" >&2
    exit 1
fi

cmake --build "$WORK" --parallel "$(nproc 2>/dev/null || echo 2)"

rm -f host_* client_*
for program in "$WORK"/host_* "$WORK"/client_*; do
    if [ -f "$program" ] && [ -x "$program" ]; then
        cp "$program" .
    fi
done

echo "Собрано:" host_* client_*
