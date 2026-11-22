#!/usr/bin/env bash
set -u
set -o pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1
./build.sh || exit 1
output=$(mktemp ./tests/reminder-tests.XXXXXX) || exit 1
trap 'rm -f -- "$output"' EXIT
trap 'exit 1' HUP INT TERM
g++ -std=c++17 -Wall -Wextra -Werror -O2 -Isrc tests/tests.cpp src/events.cpp src/daemon.cpp -o "$output" -lutil || exit 1
chmod 755 -- "$output" || exit 1
"$output" "$PWD/reminder"
