#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/variant3-build.XXXXXX")"
BIN_DIR="$ROOT_DIR/bin"

cleanup() {
    rm -rf "$BUILD_DIR"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -p "$BIN_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DWOLF_BUILD_GUI=ON \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

mapfile -d '' HOST_BINARIES < <(find "$BUILD_DIR" -maxdepth 1 -type f -executable -name 'host_*' -print0)
if ((${#HOST_BINARIES[@]} == 0)); then
    echo "No host executables were produced." >&2
    exit 1
fi

find "$BIN_DIR" -maxdepth 1 -type f -name 'host_*' -delete
cp "${HOST_BINARIES[@]}" "$BIN_DIR/"

echo "Build complete. Executables are in: $BIN_DIR"
