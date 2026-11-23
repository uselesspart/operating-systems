#!/bin/sh
set -eu
cd "$(dirname "$0")"
if [ "$(uname -s)" != Linux ]; then
    echo 'Для сборки нужен Linux.' >&2
    exit 1
fi
testing=OFF
case "${1:-}" in
    '') ;;
    test) testing=ON ;;
    *) echo 'Использование: ./build.sh [test]' >&2; exit 1 ;;
esac
build_dir=$(mktemp -d .build.XXXXXX)
trap 'rm -rf "$build_dir"' EXIT
trap 'exit 1' HUP INT TERM
cmake -S . -B "$build_dir" -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING="$testing"
cmake --build "$build_dir" --parallel "$(getconf _NPROCESSORS_ONLN)"
if [ "$testing" = ON ]; then
    ctest --test-dir "$build_dir" --output-on-failure --parallel 3
fi
