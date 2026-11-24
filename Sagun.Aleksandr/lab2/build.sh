#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "$0")" && pwd)"
build_dir="$(mktemp -d)"

trap 'rm -rf -- "$build_dir"' EXIT

cmake -S "$project_dir" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$project_dir/bin"

cmake --build "$build_dir" --parallel