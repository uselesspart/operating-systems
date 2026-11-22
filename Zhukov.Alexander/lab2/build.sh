#!/usr/bin/env bash
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/os-chat-build.XXXXXX")"

trap 'rm -rf -- "$build_dir"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

cmake -S "$project_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" --parallel 2

mkdir -p -- "$project_dir/bin"
for program in "$build_dir"/bin/host_* "$build_dir"/bin/client_*; do
    [[ -f "$program" ]] || continue
    install -m 755 "$program" "$project_dir/bin/"
done
printf 'Built: %s\n' "$project_dir/bin"
