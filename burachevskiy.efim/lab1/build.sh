#!/usr/bin/env bash
set -u
set -o pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")" || exit 1
command -v g++ >/dev/null 2>&1 || { printf 'Нужен g++ (пакет build-essential).\n' >&2; exit 1; }
output=$(mktemp ./reminder.build.XXXXXX) || exit 1
trap 'rm -f -- "$output"' EXIT
trap 'exit 1' HUP INT TERM
if ! g++ -std=c++17 -Wall -Wextra -Werror -O2 src/*.cpp -o "$output"; then
    exit 1
fi
chmod 755 -- "$output" || exit 1
mv -f -- "$output" ./reminder || exit 1
printf 'Собрано: reminder\n'
