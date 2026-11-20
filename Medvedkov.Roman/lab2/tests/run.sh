#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
make test
bash tests/build_checks.sh
python3 tests/runtime_checks.py
