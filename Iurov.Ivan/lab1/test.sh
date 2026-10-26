#!/usr/bin/env bash
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
"${SCRIPT_DIR}/build.sh"
ctest --test-dir "${SCRIPT_DIR}/build" --output-on-failure
