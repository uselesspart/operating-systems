#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")"

if (( $# > 1 )); then
    echo "Usage: $0 [--test]" >&2
    exit 2
fi

case "${1:-}" in
    "") run_tests=false ;;
    --test) run_tests=true ;;
    *)
        echo "Usage: $0 [--test]" >&2
        exit 2
        ;;
esac

# Keep CMake cache and object files outside the source tree.
temporary_build="$(mktemp -d "${TMPDIR:-/tmp}/lab2-cmake.XXXXXXXX")"
trap 'rm -rf -- "$temporary_build"' EXIT

cmake -S . -B "$temporary_build"
cmake --build "$temporary_build" --parallel

if "$run_tests"; then
    ctest --test-dir "$temporary_build" --output-on-failure
fi

# Keep only finished host executables in build/.
executables=()
for executable in "$temporary_build"/host_*; do
    if [[ -f "$executable" && -x "$executable" ]]; then
        executables+=("$executable")
    fi
done

if (( ${#executables[@]} == 0 )); then
    echo "Build error: no host_* executables were produced" >&2
    exit 1
fi

rm -rf -- build
mkdir -p build
for executable in "${executables[@]}"; do
    install -m 755 -- "$executable" "build/$(basename "$executable")"
done

echo "Build completed: ${#executables[@]} host executable(s) in build/"
