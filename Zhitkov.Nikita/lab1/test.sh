#!/bin/bash
# Полный прогон: сборка, unit-тесты, интеграционные тесты
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "Building"
"$ROOT/build.sh"

echo
echo "Unit tests"
"$ROOT/tests/unit_tests/run.sh"

echo
echo "Integration tests"
"$ROOT/tests/integration_tests/run.sh"

echo
echo "All tests PASSED"