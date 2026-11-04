#!/usr/bin/env bash
set -euo pipefail

sanitize=0
case "${1:-}" in
    --sanitize) sanitize=1; shift ;;
    -h|--help) printf '%s\n' 'Usage: ./tests/run.sh [--sanitize]'; exit 0 ;;
    '') ;;
    *) printf '%s\n' 'Usage: ./tests/run.sh [--sanitize]' >&2; exit 1 ;;
esac
if [[ "$#" -ne 0 ]]; then
    printf '%s\n' 'Usage: ./tests/run.sh [--sanitize]' >&2
    exit 1
fi

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/os-chat-tests.XXXXXX")"

trap 'rm -rf -- "$build_dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "$sanitize" -eq 1 ]]; then
    flags='-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
    export CFLAGS="${CFLAGS:-} $flags"
    export CXXFLAGS="${CXXFLAGS:-} $flags"
    export LDFLAGS="${LDFLAGS:-} -fsanitize=address,undefined"
    mkdir -p -- "$build_dir/reports"
    export ASAN_OPTIONS="detect_leaks=1:halt_on_error=1:log_path=$build_dir/reports/asan"
    export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:log_path=$build_dir/reports/ubsan"
fi

if ! cmake -S "$project_dir" -B "$build_dir" -DBUILD_TESTING=ON \
    -DCMAKE_BUILD_TYPE=Debug > "$build_dir/build.log" 2>&1; then
    cat "$build_dir/build.log" >&2
    exit 1
fi
if ! cmake --build "$build_dir" --parallel 2 >> "$build_dir/build.log" 2>&1; then
    cat "$build_dir/build.log" >&2
    exit 1
fi

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
code=0
for program in "$build_dir"/bin/run_tests_*; do
    "$program" "$project_dir" || code=1
done
if [[ "$sanitize" -eq 1 ]]; then
    "$build_dir/bin/run_tests_fifo" --reports "$build_dir/reports" || code=1
fi
exit "$code"
