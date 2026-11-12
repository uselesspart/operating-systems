#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/disk-monitor-tests.XXXXXX")"

trap 'rm -rf -- "$build_dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

compiler_flags=(-std=c++17 -Wall -Werror)
program="$project_dir/bin/disk_monitor"

if [[ "${1:-}" == "--sanitize" ]]; then
    shift
    compiler_flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    export OS_LAB_SANITIZER_DIR="$build_dir/reports"
    export ASAN_OPTIONS='detect_leaks=1:abort_on_error=1'
    export UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1'
    mkdir -p "$OS_LAB_SANITIZER_DIR"
    cmake -S "$project_dir" -B "$build_dir/program" -DCMAKE_BUILD_TYPE=Debug \
        -DCMAKE_CXX_FLAGS='-O1 -fsanitize=address,undefined -fno-omit-frame-pointer' \
        -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
    cmake --build "$build_dir/program" --parallel
    program="$build_dir/program/bin/disk_monitor"
fi

c++ "${compiler_flags[@]}" -I "$project_dir/include" \
    "$project_dir/tests/run_tests.cpp" "$project_dir/src/file_watcher.cpp" \
    "$project_dir/src/logger.cpp" \
    -o "$build_dir/run_tests"
"$build_dir/run_tests" "$program" "$@"
