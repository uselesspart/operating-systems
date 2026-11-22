#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec ./tests/run_tests.sh
