#!/bin/sh
set -eu

cd "$(dirname "$0")"

BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"
cp "$BUILD_DIR/reminder_daemon" ./reminder_daemon

echo "Готово: $(pwd)/reminder_daemon"
