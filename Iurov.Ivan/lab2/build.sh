#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/wolf-build.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DWOLF_BUILD_GUI="${WOLF_BUILD_GUI:-ON}" \
    -DWOLF_BUILD_TESTS=OFF

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

mapfile -d '' BINARIES < <(find "$BUILD_DIR" -maxdepth 1 -type f -executable -name 'host_*' -print0)
if ((${#BINARIES[@]} == 0)); then
    echo "Не собрано ни одного исполняемого файла host_*" >&2
    exit 1
fi

find "$ROOT_DIR" -maxdepth 1 -type f -name 'host_*' -delete
cp "${BINARIES[@]}" "$ROOT_DIR/"

echo "Собрано:"
for binary in "${BINARIES[@]}"; do
    basename "$binary"
done
