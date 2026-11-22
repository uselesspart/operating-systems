#!/usr/bin/env bash
set -euo pipefail

lab_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$lab_dir/scripts/build_options.sh"
parse_build_options build ON OFF "$@"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gutsalov-lab1-build.XXXXXX")
binary_tmp=''
cleanup() {
    rm -rf -- "$build_dir"
    if [[ -n "$binary_tmp" ]]; then
        rm -f -- "$binary_tmp"
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake -S "$lab_dir" -B "$build_dir" "${CMAKE_OPTIONS[@]}"
cmake --build "$build_dir" --parallel
if [[ "$ENABLE_TESTING" == ON ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
fi
binary_tmp=$(mktemp "$lab_dir/.lab1.XXXXXX")
install -m 755 "$build_dir/bin/lab1" "$binary_tmp"
mv -f -- "$binary_tmp" "$lab_dir/lab1"
printf 'Executable: %s/lab1\n' "$lab_dir"
