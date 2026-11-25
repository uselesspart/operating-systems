#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$@"
make -C "$BUILD_DIR" -j"$(nproc)"
