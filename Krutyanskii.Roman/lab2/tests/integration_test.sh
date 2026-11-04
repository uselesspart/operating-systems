#!/usr/bin/env bash
# Headless smoke test for the lab2 Qt hosts.
# Every host_<type> must complete several rounds of the wolf/kid game with no
# timeout. Requires a display (WSLg, or run under 'xvfb-run'); skips cleanly
# when none is available.

set -uo pipefail

TEST_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TEST_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

if [[ -z "${DISPLAY:-}" ]]; then
    echo "SKIP: no DISPLAY set (use WSLg or 'xvfb-run bash integration_test.sh')"
    exit 0
fi

PASS=0
FAIL=0

cleanup() {
    pkill -x host_mmap 2>/dev/null
    pkill -x host_shm 2>/dev/null
    pkill -x host_pipe 2>/dev/null
    rm -f /dev/shm/lab2_shm
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
    timeout 16 "$bin" >"$log" 2>&1 || true
    cleanup

    rounds="$(grep -c "Kid sent" "$log" || true)"
    if grep -qi "timeout" "$log"; then
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
