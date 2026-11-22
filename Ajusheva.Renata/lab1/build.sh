#!/bin/sh

# Запуск:              ./build.sh      (или sh build.sh)
# Другой компилятор:   CXX=clang++ ./build.sh

set -eu

cd "$(dirname "$0")"

CXX=${CXX:-g++}
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror"
TARGET=diskmond

if ! command -v "${CXX%% *}" >/dev/null 2>&1; then
    echo "build.sh: compiler '$CXX' not found (Debian/Ubuntu: sudo apt install g++)" >&2
    exit 1
fi

OBJ_DIR=$(mktemp -d "${TMPDIR:-/tmp}/diskmond-build.XXXXXX")
running="" 

cleanup()
{
   
    for job in $running; do
        pkill -TERM -P "${job%%:*}" 2>/dev/null || true
        kill "${job%%:*}" 2>/dev/null || true
    done
    wait 2>/dev/null || true
    rm -rf "$OBJ_DIR"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM


rm -f "$TARGET"


failed=0
collect()
{
    for job in $running; do
        if ! wait "${job%%:*}"; then
            failed=1
        fi
        cat "$OBJ_DIR/${job#*:}.log"
    done
    running=""
}


jobs_limit=$(nproc 2>/dev/null || echo 2)
count=0
for source in src/*.cpp; do
    name=$(basename "$source" .cpp)
    echo "  CXX   $source"

    $CXX $CXXFLAGS -c "$source" -o "$OBJ_DIR/$name.o" >"$OBJ_DIR/$name.log" 2>&1 &
    running="$running $!:$name"
    count=$((count + 1))
    if [ "$count" -ge "$jobs_limit" ]; then
        collect
        count=0
    fi
done
collect

if [ "$failed" -ne 0 ]; then
    echo "build.sh: compilation failed" >&2
    exit 1
fi

echo "  LD    $TARGET"
# shellcheck disable=SC2086
$CXX $CXXFLAGS "$OBJ_DIR"/*.o -o "$TARGET"

echo "Built $(pwd)/$TARGET"
