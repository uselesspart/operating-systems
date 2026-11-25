
#!/usr/bin/env bash

set -e

cd "$(dirname "$0")"

BUILD_DIR=".cmake-build"
OUTPUT_DIR="build"

rm -rf "$BUILD_DIR" "$OUTPUT_DIR"

cleanup() {
    rm -rf "$BUILD_DIR"
}

trap cleanup EXIT

cmake -S . -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$(nproc)"

mkdir -p "$OUTPUT_DIR"
cp "$BUILD_DIR"/host_* "$OUTPUT_DIR"/

echo "Сборка завершена."
