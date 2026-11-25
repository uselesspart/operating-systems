#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
# --fresh removes CMakeCache.txt and CMakeFiles, including old object files.
build_dir=${BUILD_DIR:-"$project_dir/build"}
cmake --fresh -S "$project_dir" -B "$build_dir" "$@"
cmake --build "$build_dir" --parallel "${JOBS:-4}"
