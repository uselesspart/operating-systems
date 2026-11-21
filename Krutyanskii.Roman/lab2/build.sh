#!/bin/bash
set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf -- "$WORK_DIR"' EXIT

cmake -S "$PROJECT_DIR" -B "$WORK_DIR"
cmake --build "$WORK_DIR" --parallel "$(nproc)"

# Publish only executables. CMake metadata, objects and moc files are temporary.
rm -rf -- "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
for binary in "$WORK_DIR"/host_* "$WORK_DIR"/client_*; do
    if [[ -f "$binary" && -x "$binary" ]]; then
        install -m 755 "$binary" "$BUILD_DIR/$(basename "$binary")"
    fi
done
echo "Built executables in $BUILD_DIR"
