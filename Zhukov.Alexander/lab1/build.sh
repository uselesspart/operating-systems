#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/disk-monitor-build.XXXXXX")"

trap 'rm -rf -- "$build_dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel

mkdir -p -- "$project_dir/bin"
install -m 755 "$build_dir/bin/disk_monitor" "$project_dir/bin/disk_monitor"
printf 'Built: %s\n' "$project_dir/bin/disk_monitor"
