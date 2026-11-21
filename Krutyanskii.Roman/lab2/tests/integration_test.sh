#!/usr/bin/env bash
# Headless smoke test for the lab2 Qt hosts.
# Every host_<type> must complete several rounds of the wolf/kid game with no
# timeout. Qt's offscreen backend also works without a display.

set -uo pipefail

TEST_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TEST_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

PASS=0
FAIL=0

cleanup() {
    if [[ -n "${log:-}" ]]; then rm -f "$log"; fi
}
trap cleanup EXIT

for type in mmap shm pipe; do
    bin="$BUILD_DIR/host_$type"
    if [[ ! -x "$bin" ]]; then
        echo "FAIL: $bin not built (run 'bash build.sh' first)"
        ((FAIL++)) || true
        continue
    fi

    log="$(mktemp)"
    result=0
    timeout 16 "$bin" >"$log" 2>&1 || result=$?

    rounds="$(grep -c "Kid sent" "$log" || true)"
    if [[ "$result" -ne 0 && "$result" -ne 124 ]]; then
        echo "FAIL: host_$type exited with status $result"
        cat "$log"
        ((FAIL++)) || true
    elif grep -qi "timeout" "$log"; then
        echo "FAIL: host_$type reported a timeout"
        sed 's/^/    /' "$log"
        ((FAIL++)) || true
    elif [[ "$rounds" -lt 2 ]]; then
        echo "FAIL: host_$type completed only $rounds round(s)"
        sed 's/^/    /' "$log"
        ((FAIL++)) || true
    else
        echo "PASS: host_$type completed $rounds rounds without timeout"
        ((PASS++)) || true
    fi
    rm -f "$log"
done

echo ""
echo "Results: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]
