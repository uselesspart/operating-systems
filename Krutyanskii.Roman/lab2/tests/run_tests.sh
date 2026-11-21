#!/usr/bin/env bash
# Runs all lab2 tests: compiles and executes the unit tests, then runs the
# headless integration smoke test.
set -uo pipefail

TEST_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TEST_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

mkdir -p "$BUILD_DIR"
TEST_BUILD="$(mktemp -d)"
trap 'rm -rf -- "$TEST_BUILD"' EXIT

echo "=== compiling unit tests ==="
if ! g++ -std=c++17 -Wall -Werror -I"$PROJECT_DIR/src" \
        "$TEST_DIR/unit_tests.cpp" \
        "$PROJECT_DIR/src/common.cpp" \
        "$PROJECT_DIR/src/conn_mmap.cpp" \
        "$PROJECT_DIR/src/conn_shm.cpp" \
        "$PROJECT_DIR/src/conn_pipe.cpp" \
        -o "$TEST_BUILD/unit_tests_bin" -pthread; then
    echo "FAIL: unit tests did not compile"
    exit 1
fi

rc=0

echo "=== unit tests ==="
if "$TEST_BUILD/unit_tests_bin"; then
    echo "unit tests: OK"
else
    echo "unit tests: FAILED"
    rc=1
fi

echo "=== build regression tests ==="
if bash "$TEST_DIR/build_test.sh"; then
    echo "build tests: OK"
else
    rc=1
fi

echo "=== integration tests ==="
if bash "$TEST_DIR/integration_test.sh"; then
    echo "integration tests: OK"
else
    echo "integration tests: FAILED"
    rc=1
fi

echo "=== window shutdown regression tests (Qt offscreen) ==="
if cmake -S "$PROJECT_DIR" -B "$TEST_BUILD" -DLAB2_BUILD_TESTS=ON &&
   cmake --build "$TEST_BUILD" --target shutdown_mmap shutdown_shm shutdown_pipe --parallel "$(nproc)" &&
   ctest --test-dir "$TEST_BUILD" --output-on-failure; then
    echo "shutdown tests: OK"
else
    echo "shutdown tests: FAILED"
    rc=1
fi

echo ""
if [[ "$rc" -eq 0 ]]; then
    echo "=== ALL LAB2 TESTS PASSED ==="
else
    echo "=== SOME LAB2 TESTS FAILED ==="
fi
exit "$rc"
