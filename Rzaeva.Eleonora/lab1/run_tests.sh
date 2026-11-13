#!/usr/bin/env bash
#
# Сборка и запуск тестов (GoogleTest + ctest).
#
#   ./run_tests.sh                                  # все тесты
#   ./run_tests.sh -L unit                          # только unit-тесты
#   ./run_tests.sh -L component                     # только компонентные
#   ./run_tests.sh --gtest_filter='*Sighup*'        # фильтр по именам gtest-кейсов
#   ./run_tests.sh --output-on-failure -V           # любые аргументы ctest
#
set -euo pipefail
cd "$(dirname "$0")"

build_dir="${BUILD_DIR:-build}"

cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON >/dev/null
cmake --build "$build_dir" -j"$(nproc)"

# Удобный проброс gtest-фильтра прямо в тестовые бинарники.
if [[ "${1:-}" == --gtest_filter=* ]]; then
    filter="$1"; shift
    "$build_dir/tests/unit_tests" "$filter" "$@"
    "$build_dir/tests/component_tests" "$filter" "$@"
    exit
fi

ctest --test-dir "$build_dir" --output-on-failure "$@"
