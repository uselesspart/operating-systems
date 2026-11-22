#!/usr/bin/env bash
set -euo pipefail

lab_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$lab_dir/scripts/build_options.sh"
parse_build_options build ON OFF "$@"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gutsalov-lab2-build.XXXXXX")
staging_dir=''
cleanup() {
    rm -rf -- "$build_dir"
    if [[ -n "$staging_dir" ]]; then rm -rf -- "$staging_dir"; fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake -S "$lab_dir" -B "$build_dir" "${CMAKE_OPTIONS[@]}"
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
if [[ "$ENABLE_TESTING" == ON ]]; then
    ctest --test-dir "$build_dir" --output-on-failure
fi
staging_dir=$(mktemp -d "$lab_dir/.lab2-bin.XXXXXX")
for binary in "$build_dir"/bin/host_* "$build_dir"/bin/client_*; do
    install -m 755 "$binary" "$staging_dir/$(basename -- "$binary")"
done
for binary in "$staging_dir"/*; do
    mv -f -- "$binary" "$lab_dir/$(basename -- "$binary")"
done
printf 'Executables: %s/{host,client}_{pipe,fifo,sock}\n' "$lab_dir"
