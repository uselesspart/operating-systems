#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
if (( $# > 1 )) || [[ ${1:-} != '' && ${1:-} != --test ]]; then
    printf 'Usage: bash build.sh [--test]\n' >&2
    exit 1
fi
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
testing=OFF
[[ ${1:-} != --test ]] || testing=ON
cmake -S . -B "$temporary" -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$testing"
cmake --build "$temporary" --parallel "${JOBS:-2}"
if [[ $testing == ON ]]; then ctest --test-dir "$temporary" --output-on-failure --parallel "${JOBS:-2}"; fi
for program in "$temporary"/bin/host_*; do
    install -m 755 -- "$program" "./$(basename -- "$program")"
done
