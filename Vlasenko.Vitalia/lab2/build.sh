#!/usr/bin/env bash

set -euo pipefail

PROJECT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
OUTPUT_DIR="${PROJECT_DIR}/bin"

BUILD_TESTING=OFF

case "${1:-}" in
    "")
        ;;
    --test)
        BUILD_TESTING=ON
        ;;
    *)
        echo "Usage: $0 [--test]" >&2
        exit 1
        ;;
esac

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/lab2-build.XXXXXXXX")"

# Промежуточные файлы удаляются даже при ошибке сборки.
trap 'rm -rf -- "$BUILD_DIR"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake \
    -S "$PROJECT_DIR" \
    -B "$BUILD_DIR" \
    -G "Unix Makefiles" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_TESTING="$BUILD_TESTING"

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

if [[ "$BUILD_TESTING" == "ON" ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

mkdir -p "$OUTPUT_DIR"

find "$OUTPUT_DIR" -maxdepth 1 -type f \
    \( -name 'host_*' -o -name 'client_*' \) -delete

find "$BUILD_DIR/bin" -maxdepth 1 -type f \
    \( -name 'host_*' -o -name 'client_*' \) \
    -exec cp -- {} "$OUTPUT_DIR/" \;

echo "Build completed successfully."
echo "Executables: $OUTPUT_DIR"
