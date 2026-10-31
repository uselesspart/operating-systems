#!/usr/bin/env bash
set -euo pipefail

lab_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
source "$lab_dir/scripts/build_options.sh"
parse_build_options tests OFF ON "$@"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gutsalov-lab1-tests.XXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake -S "$lab_dir" -B "$build_dir" "${CMAKE_OPTIONS[@]}"
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure "${CTEST_OPTIONS[@]}"
