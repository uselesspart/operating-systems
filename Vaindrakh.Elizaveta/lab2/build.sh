#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
project_dir="$PWD"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/wolf-lab2-build.XXXXXXXX")"
trap 'rm -rf -- "$build_dir"' EXIT
test_mode=OFF
if [[ ${1:-} == --test ]]; then
    test_mode=ON
elif [[ $# -gt 0 ]]; then
    echo "Usage: bash build.sh [--test]" >&2
    exit 2
fi
cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING="$test_mode"
cmake --build "$build_dir" --parallel "${BUILD_JOBS:-2}"
if [[ $test_mode == ON ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
fi
for executable in "$build_dir"/bin/host_*; do
    install -m 755 "$executable" "$project_dir/$(basename -- "$executable")"
done
echo "Ready: $project_dir/host_mmap, host_shm, host_pipe"
