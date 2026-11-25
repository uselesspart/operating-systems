#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

case "${1:-build}" in
    build) tests=OFF ;;
    test) tests=ON ;;
    *)
        echo "Использование: bash build.sh [build|test]" >&2
        exit 2
        ;;
esac

build_dir="$(mktemp -d "$PWD/.build.XXXXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT
cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DHOST_OUTPUT_DIRECTORY="$PWD" -DBUILD_TESTING="$tests"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
if [[ "$tests" == ON ]]; then
    ctest --test-dir "$build_dir" --output-on-failure --parallel 3
fi
