#!/bin/sh
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d)
trap 'rm -rf "$build"' EXIT
case "${1:-}" in
    "") tests=OFF ;;
    test) tests=ON ;;
    *) echo "Использование: $0 [test]" >&2; exit 2 ;;
esac
cmake -S . -B "$build" -DBUILD_TESTING="$tests"
cmake --build "$build" --parallel 4
if [ "$tests" = ON ]; then
    ctest --test-dir "$build" --output-on-failure
fi
cmake --install "$build" --prefix "$PWD"
