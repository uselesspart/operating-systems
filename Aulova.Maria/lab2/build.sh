#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"
BUILD_DIR=build

build() {
    trap 'rm -rf "$BUILD_DIR"' EXIT
    cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
    cmake --build "$BUILD_DIR" --parallel "$(nproc)"
}

run_tests() {
    ctest --test-dir "$BUILD_DIR" --output-on-failure
}

clean() {
    rm -rf "$BUILD_DIR"
    rm -f host_*
}

case "${1:-build}" in
    build)   build ;;
    test)    build; run_tests ;;
    clean)   clean ;;
    rebuild) clean; build ;;
    *)       echo "Использование: $0 [build|test|clean|rebuild]" >&2; exit 2 ;;
esac
