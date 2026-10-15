#!/usr/bin/env bash
# Wrapper to run all lab1 integration tests.
# No external dependencies.
set -euo pipefail
cd "$(dirname "$0")"
exec bash test_lab1.sh "$@"
