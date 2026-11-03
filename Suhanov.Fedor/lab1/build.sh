#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"

build_dir=$(mktemp -d)
trap 'rm -rf "$build_dir"' EXIT

cmake -S . -B "$build_dir" -DCMAKE_RUNTIME_OUTPUT_DIRECTORY="$PWD"
cmake --build "$build_dir"
