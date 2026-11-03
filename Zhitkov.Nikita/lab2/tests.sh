#!/bin/bash
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"

echo
"$ROOT/tests/run_unit_tests.sh"

echo
"$ROOT/tests/integration/run.sh"