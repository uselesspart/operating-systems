#!/usr/bin/env bash
# Запуск тестов: ./run_tests.sh [аргументы ctest, например -L unit, -R Sighup]
set -euo pipefail
cd "$(dirname "$0")"

BUILD="$(mktemp -d)"
trap 'rm -rf "$BUILD"' EXIT

cmake -S tests -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug > /dev/null
cmake --build "$BUILD" -j"$(nproc)"
ctest --test-dir "$BUILD" --output-on-failure "$@"
