#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT/.build"

rm -rf "$BUILD_DIR"
rm -f "$ROOT"/host_mq "$ROOT"/host_fifo "$ROOT"/host_sock

cmake -S "$ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel

for exe in "$BUILD_DIR"/host_*; do
    if [[ -x "$exe" ]]; then
        cp "$exe" "$ROOT/"
    fi
done

rm -rf "$BUILD_DIR"

echo "Built executables:"
ls -1 "$ROOT"/host_* 2>/dev/null || true
