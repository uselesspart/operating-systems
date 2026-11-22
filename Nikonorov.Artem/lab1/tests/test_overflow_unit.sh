#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf -- "$TMP_DIR"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic -I"${ROOT_DIR}/include" \
  "${ROOT_DIR}/tests/test_overflow_unit.cpp" -o "$TMP_DIR/test_overflow_unit"
"$TMP_DIR/test_overflow_unit"
