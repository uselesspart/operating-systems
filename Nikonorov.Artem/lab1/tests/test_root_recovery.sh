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

WATCH="$TMP_DIR/watch-root"
OUTSIDE="$TMP_DIR/outside"
STABLE="$TMP_DIR/stable-root"
mkdir -p "$WATCH/nested" "$OUTSIDE" "$STABLE"
CONFIG="$TMP_DIR/move-root.conf"
PID_FILE="$TMP_DIR/move-root.pid"
CAPTURE="$TMP_DIR/move-root.log"
OUTPUT="$TMP_DIR/move-root.out"
: > "$CAPTURE"
printf 'watch_dir=%s\nwatch_dir=%s\n' "$WATCH" "$STABLE" > "$CONFIG"
start_monitor "$CONFIG" "$PID_FILE" "$CAPTURE" "$OUTPUT"
printf 'before move\n' > "$WATCH/nested/before.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/nested/before.txt" || fail "initial root watch did not work"

mv "$WATCH" "$OUTSIDE/moved-root"
wait_for_record "$CAPTURE" "facility=24" "recovery anchor=" "root='$WATCH'" || { tail -n 60 "$CAPTURE" >&2 || true; fail "root move did not replace the old subtree watches with a recovery anchor"; }
sleep 0.2
printf 'still active\n' > "$STABLE/other-root-still-active.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$STABLE/other-root-still-active.txt" || fail "temporary loss of one root disrupted the other configured root"
printf 'late outside write\n' > "$OUTSIDE/moved-root/nested/late-outside.txt"
sleep 0.4
if record_exists "$CAPTURE" "facility=128" "path=$WATCH/nested/late-outside.txt"; then
  fail "stale watch continued to report a moved-out root under its old configured path"
fi

mkdir -p "$WATCH/nested"
printf 'recreated root content\n' > "$WATCH/nested/reappeared.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/nested/reappeared.txt" || fail "recreated root or its existing descendants were not discovered"
printf 'after recovery\n' > "$WATCH/nested/after-recovery.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/nested/after-recovery.txt" || fail "recreated root did not remain actively monitored"
rm -rf -- "$WATCH"
sleep 0.3
mkdir -p "$WATCH/nested"
printf 'after deletion\n' > "$WATCH/nested/recreated-after-delete.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/nested/recreated-after-delete.txt" || fail "deleted root did not recover after being recreated"
stop_monitor
[[ ! -e "$PID_FILE" ]] || fail "PID file remained after root-move test"

MISSING_ROOT="$TMP_DIR/not-created/intermediate/root"
CONFIG2="$TMP_DIR/missing-root.conf"
PID_FILE2="$TMP_DIR/missing-root.pid"
CAPTURE2="$TMP_DIR/missing-root.log"
OUTPUT2="$TMP_DIR/missing-root.out"
: > "$CAPTURE2"
printf 'watch_dir=%s\n' "$MISSING_ROOT" > "$CONFIG2"
start_monitor "$CONFIG2" "$PID_FILE2" "$CAPTURE2" "$OUTPUT2"
wait_for_record "$CAPTURE2" "facility=24" "recovery anchor=" "root='$MISSING_ROOT'" || fail "startup did not retain a recovery anchor for an absent root"
printf 'unrelated sibling\n' > "$TMP_DIR/unrelated.txt"
sleep 0.2
if record_exists "$CAPTURE2" "facility=128" "path=$TMP_DIR/unrelated.txt"; then
  fail "unrelated activity in the recovery-anchor parent was logged as monitored content"
fi
mkdir -p "$MISSING_ROOT/nested"
printf 'created later\n' > "$MISSING_ROOT/nested/created-after-start.txt"
wait_for_record "$CAPTURE2" "facility=128" "path=$MISSING_ROOT/nested/created-after-start.txt" || fail "root absent at startup did not become monitored after creation"
stop_monitor
[[ ! -e "$PID_FILE2" ]] || fail "PID file remained after absent-root test"

DEFAULT_CWD="$TMP_DIR/default-cwd"
DEFAULT_WATCH="$TMP_DIR/default-watch"
DEFAULT_WATCH2="$TMP_DIR/default-watch-2"
mkdir -p "$DEFAULT_CWD" "$DEFAULT_WATCH" "$DEFAULT_WATCH2"
printf 'watch_dir=%s\n' "$DEFAULT_WATCH" > "$DEFAULT_CWD/disk-monitor.conf"
DEFAULT_PID="$TMP_DIR/default.pid"
DEFAULT_CAPTURE="$TMP_DIR/default.log"
DEFAULT_OUTPUT="$TMP_DIR/default.out"
: > "$DEFAULT_CAPTURE"
(
  cd "$DEFAULT_CWD"
  exec env DISKMON_TEST_LOG="$DEFAULT_CAPTURE" LD_PRELOAD="$CAPTURE_SO" \
    "$BIN" --pid-file "$DEFAULT_PID" >"$DEFAULT_OUTPUT" 2>&1
) &
LAUNCHER_PID=$!
wait "$LAUNCHER_PID" || fail "daemon launch using the default relative config path failed"
wait_for_pid_file "$DEFAULT_PID" || fail "default ./disk-monitor.conf was not loaded from the working directory"
MONITOR_PID="$(cat "$DEFAULT_PID")"
[[ "$MONITOR_PID" != "$LAUNCHER_PID" ]] || fail "default-config test did not exercise daemonization"
printf 'default config works\n' > "$DEFAULT_WATCH/default-config-event.txt"
wait_for_record "$DEFAULT_CAPTURE" "facility=128" "path=$DEFAULT_WATCH/default-config-event.txt" || fail "default config root was not monitored after chdir('/')"
printf 'watch_dir=%s\n' "$DEFAULT_WATCH2" > "$DEFAULT_CWD/disk-monitor.conf"
BEFORE_RELOAD="$(wc -l < "$DEFAULT_CAPTURE")"
kill -HUP "$MONITOR_PID"
wait_for_record_since "$DEFAULT_CAPTURE" "$BEFORE_RELOAD" "facility=24" "configuration reloaded" || fail "SIGHUP did not reload the same absolute config file after daemonization"
printf 'reloaded relative-config path\n' > "$DEFAULT_WATCH2/reloaded-config-event.txt"
wait_for_record "$DEFAULT_CAPTURE" "facility=128" "path=$DEFAULT_WATCH2/reloaded-config-event.txt" || fail "SIGHUP read config from the wrong working directory"
kill -TERM "$MONITOR_PID"
for _ in $(seq 1 100); do
  [[ ! -e "$DEFAULT_PID" ]] && break
  sleep 0.05
done
[[ ! -e "$DEFAULT_PID" ]] || fail "PID file remained after default-config daemon shutdown"
MONITOR_PID=""

BAD_CONFIG="$TMP_DIR/bad-early.conf"
EARLY_PID="$TMP_DIR/early.pid"
EARLY_CAPTURE="$TMP_DIR/early.log"
EARLY_OUTPUT="$TMP_DIR/early.out"
printf 'not_a_directive\n' > "$BAD_CONFIG"
: > "$EARLY_CAPTURE"
if DISKMON_TEST_LOG="$EARLY_CAPTURE" LD_PRELOAD="$CAPTURE_SO" \
   "$BIN" --foreground --config "$BAD_CONFIG" --pid-file "$EARLY_PID" >"$EARLY_OUTPUT" 2>&1; then
  fail "malformed initial config was unexpectedly accepted"
fi
assert_record "$EARLY_CAPTURE" "facility=24" "expected key=value" || fail "early config failure was not sent to LOG_DAEMON"
grep -F "expected key=value" "$EARLY_OUTPUT" >/dev/null || fail "early config failure was not printed to stderr"
[[ ! -e "$EARLY_PID" ]] || fail "PID file was created for malformed config"

LOCK_CAPTURE="$TMP_DIR/lock-early.log"
LOCK_OUTPUT="$TMP_DIR/lock-early.out"
LOCK_CONFIG="$TMP_DIR/lock-early.conf"
printf 'watch_dir=%s\n' "$STABLE" > "$LOCK_CONFIG"
: > "$LOCK_CAPTURE"
if DISKMON_TEST_LOG="$LOCK_CAPTURE" LD_PRELOAD="$CAPTURE_SO" \
   "$BIN" --foreground --config "$LOCK_CONFIG" --pid-file "$TMP_DIR/no-such-dir/monitor.pid" >"$LOCK_OUTPUT" 2>&1; then
  fail "startup unexpectedly succeeded when the PID/lock directory was missing"
fi
assert_record "$LOCK_CAPTURE" "facility=24" "cannot open lock file" || fail "early PID-lock failure was not sent to LOG_DAEMON"
grep -F "cannot open lock file" "$LOCK_OUTPUT" >/dev/null || fail "early PID-lock failure was not printed to stderr"

echo "PASS: root move-out/delete/recreation, absent root at startup, unrelated anchor-event filtering, default config path, early config/PID-lock logging"
