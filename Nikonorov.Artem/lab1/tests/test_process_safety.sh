#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/disk-monitor"
source "${ROOT_DIR}/tests/support/common.sh"
ensure_capture_library
TMP_DIR="$(mktemp -d)"
MONITOR_PID=""

cleanup() {
  stop_monitor
  rm -rf -- "$TMP_DIR"
  cleanup_capture_library
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

WATCH="$TMP_DIR/watch"
CONFIG="$TMP_DIR/monitor.conf"
PID_FILE="$TMP_DIR/monitor.pid"
OUTPUT="$TMP_DIR/monitor.out"
mkdir -p "$WATCH"
printf 'watch_dir=%s\n' "$WATCH" > "$CONFIG"

CLI_CAPTURE="$TMP_DIR/cli.log"
CLI_OUTPUT="$TMP_DIR/cli.out"
: > "$CLI_CAPTURE"
if DISKMON_TEST_LOG="$CLI_CAPTURE" LD_PRELOAD="$CAPTURE_SO" \
    "$BIN" --invalid-option-for-test >"$CLI_OUTPUT" 2>&1; then
  fail "unknown CLI option unexpectedly succeeded"
else
  CLI_STATUS=$?
fi
[[ "$CLI_STATUS" -eq 2 ]] || fail "unknown CLI option returned unexpected status $CLI_STATUS"
assert_record "$CLI_CAPTURE" "facility=24" "ident=disk-monitor" "Unknown argument: --invalid-option-for-test" \
  || fail "unknown CLI option was not logged to LOG_DAEMON"
grep -F "Unknown argument: --invalid-option-for-test" "$CLI_OUTPUT" >/dev/null \
  || fail "unknown CLI option was not printed to stderr"

"$BIN" --foreground --config "$CONFIG" --pid-file "$PID_FILE" >"$OUTPUT" 2>&1 &
MONITOR_PID=$!
wait_for_pid_file "$PID_FILE" || fail "monitor did not create its PID file"
[[ "$(cat "$PID_FILE")" == "$MONITOR_PID" ]] || fail "monitor PID file contains the wrong PID"

FIFO_PID="$TMP_DIR/fifo.pid"
mkfifo "$FIFO_PID"
set +e
timeout 2s "$BIN" --foreground --config "$CONFIG" --pid-file "$FIFO_PID" \
  >"$TMP_DIR/fifo.out" 2>&1
FIFO_STATUS=$?
set -e
[[ "$FIFO_STATUS" -ne 0 ]] || fail "FIFO PID path unexpectedly succeeded"
[[ "$FIFO_STATUS" -ne 124 && "$FIFO_STATUS" -ne 137 ]] || fail "FIFO PID path hung until timeout (status=$FIFO_STATUS)"
kill -0 "$MONITOR_PID" 2>/dev/null || fail "FIFO PID path stopped the valid monitor"
grep -F "regular, singly-linked file" "$TMP_DIR/fifo.out" >/dev/null \
  || fail "FIFO rejection was not diagnosed clearly"

SYMLINK_PID="$TMP_DIR/symlink.pid"
ln -s "$PID_FILE" "$SYMLINK_PID"
set +e
timeout 3s "$BIN" --foreground --config "$CONFIG" --pid-file "$SYMLINK_PID" \
  >"$TMP_DIR/symlink.out" 2>&1
SYMLINK_STATUS=$?
set -e
[[ "$SYMLINK_STATUS" -ne 0 ]] || fail "symbolic-link PID path unexpectedly succeeded"
[[ "$SYMLINK_STATUS" -ne 124 && "$SYMLINK_STATUS" -ne 137 ]] \
  || fail "symbolic-link PID path hung until timeout (status=$SYMLINK_STATUS)"
kill -0 "$MONITOR_PID" 2>/dev/null || fail "symbolic-link PID path stopped the valid monitor"
[[ "$(cat "$PID_FILE")" == "$MONITOR_PID" ]] || fail "valid PID file changed after symlink rejection"
grep -F "symbolic link" "$TMP_DIR/symlink.out" >/dev/null \
  || fail "symbolic-link rejection was not diagnosed clearly"

HARDLINK_PID="$TMP_DIR/hardlink.pid"
ln "$PID_FILE" "$HARDLINK_PID"
set +e
timeout 3s "$BIN" --foreground --config "$CONFIG" --pid-file "$HARDLINK_PID" \
  >"$TMP_DIR/hardlink.out" 2>&1
HARDLINK_STATUS=$?
set -e
[[ "$HARDLINK_STATUS" -ne 0 ]] || fail "hard-linked PID path unexpectedly succeeded"
[[ "$HARDLINK_STATUS" -ne 124 && "$HARDLINK_STATUS" -ne 137 ]] \
  || fail "hard-linked PID path hung until timeout (status=$HARDLINK_STATUS)"
kill -0 "$MONITOR_PID" 2>/dev/null || fail "hard-linked PID path stopped the valid monitor"
rm -f -- "$HARDLINK_PID"

stop_monitor
[[ ! -e "$PID_FILE" ]] || fail "owned PID file was not removed during normal shutdown"
echo "PASS: early CLI syslog, FIFO/symlink/hardlink PID rejection, bounded startup, existing-process safety"
