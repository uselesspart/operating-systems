#!/usr/bin/env bash

set -e

SCRIPT_DIR="$(
    cd "$(dirname "${BASH_SOURCE[0]}")"
    pwd
)"

OUTPUT="$SCRIPT_DIR/mydaemon"

echo "Cleaning old build..."
rm -f "$OUTPUT"

echo "Building..."

g++ \
    -std=c++17 \
    -Wall \
    -Werror \
    -I"$SCRIPT_DIR/include" \
    "$SCRIPT_DIR/src/main.cpp" \
    "$SCRIPT_DIR/src/Daemon.cpp" \
    "$SCRIPT_DIR/src/ConfigLoader.cpp" \
    "$SCRIPT_DIR/src/PidFile.cpp" \
    "$SCRIPT_DIR/src/ReminderTask.cpp" \
    -o "$OUTPUT"

echo "Build completed:"
echo "$OUTPUT"