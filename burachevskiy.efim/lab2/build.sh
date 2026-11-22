#!/usr/bin/env bash
set -eu
if [[ $# -gt 1 || ( $# -eq 1 && $1 != --test ) ]]; then
    printf 'Usage: %s [--test]\n' "$0" >&2
    exit 2
fi

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
command -v cmake >/dev/null || { printf 'Нужен cmake\n' >&2; exit 1; }
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

tests=OFF
if [[ $# -eq 1 ]]; then tests=ON; fi
cmake -S . -B "$work" -DCMAKE_BUILD_TYPE=Release "-DBUILD_TESTING=$tests"
cmake --build "$work" --parallel "${BUILD_JOBS:-2}"
if [[ "$tests" == ON ]]; then
    (cd -- "$work" && ctest --output-on-failure)
fi

for file in "$work"/host_*; do
    install -m 755 -- "$file" "./${file##*/}"
done
