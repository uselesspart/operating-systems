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

A="$TMP_DIR/watch/A"
B="$TMP_DIR/watch/B"
mkdir -p "$A/nested" "$B/nested" "$TMP_DIR/configs"
CONFIG="$TMP_DIR/configs/active.conf"
PID_FILE="$TMP_DIR/monitor.pid"
CAPTURE="$TMP_DIR/captured.log"
OUTPUT="$TMP_DIR/monitor.out"
: > "$CAPTURE"
printf 'watch_dir=%s\n' "$A" > "$CONFIG"
start_monitor "$CONFIG" "$PID_FILE" "$CAPTURE" "$OUTPUT"
assert_pid_owned "$PID_FILE" "$MONITOR_PID" || fail "initial PID ownership failed"

printf 'before\n' > "$A/nested/before-reload.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$A/nested/before-reload.txt" || fail "initial A tree was not monitored"

# Valid SIGHUP: B becomes active and A, including nested watches, is removed.
printf 'watch_dir=%s\n' "$B" > "$CONFIG"
kill -HUP "$MONITOR_PID"
wait_for_record "$CAPTURE" "facility=24" "configuration reloaded" || fail "valid SIGHUP reload was not logged"
assert_pid_owned "$PID_FILE" "$MONITOR_PID" || fail "PID file/process changed after valid reload"
printf 'B-active\n' > "$B/nested/after-reload-B.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$B/nested/after-reload-B.txt" || fail "new B root was not activated"
printf 'A-inactive\n' > "$A/nested/after-reload-A.txt"
sleep 0.5
if record_exists "$CAPTURE" "path=$A/nested/after-reload-A.txt"; then
  fail "old A root (or its nested watch) remained active after reload"
fi

invalid_count=0
for kind in syntax unknown empty empty_value; do
  case "$kind" in
    syntax) printf 'watch_dir\n' > "$CONFIG" ;;
    unknown) printf 'unknown_directive=%s\n' "$B" > "$CONFIG" ;;
    empty) printf '# no watch roots\n' > "$CONFIG" ;;
    empty_value) printf 'watch_dir=\n' > "$CONFIG" ;;
  esac
  REJECT_MARKER="configuration reload rejected"
  before_lines="$(wc -l < "$CAPTURE")"
  kill -HUP "$MONITOR_PID"
  wait_for_record_since "$CAPTURE" "$before_lines" "facility=24" "$REJECT_MARKER" || fail "$kind config reload rejection was not logged"
  assert_pid_owned "$PID_FILE" "$MONITOR_PID" || fail "PID/process lost after rejected $kind config"
  token="retained-B-$kind.txt"
  printf '%s\n' "$kind" > "$B/nested/$token"
  wait_for_record "$CAPTURE" "facility=128" "path=$B/nested/$token" || fail "previous B watch did not survive rejected $kind config"
  invalid_count=$((invalid_count + 1))
done
[[ "$invalid_count" -eq 4 ]] || fail "not all invalid-config cases ran"

MISSING="$TMP_DIR/missing-partial"
printf 'watch_dir=%s\nwatch_dir=%s\n' "$B" "$MISSING" > "$CONFIG"
before_lines="$(wc -l < "$CAPTURE")"
kill -HUP "$MONITOR_PID"
wait_for_record_since "$CAPTURE" "$before_lines" "facility=24" "watch root is missing, unavailable, or not a directory; retaining it for recovery: $MISSING" || fail "missing-root recovery warning missing"
wait_for_record_since "$CAPTURE" "$before_lines" "facility=24" "configuration reloaded" || fail "partial configuration was not applied"
assert_pid_owned "$PID_FILE" "$MONITOR_PID" || fail "PID/process lost after partial config"
printf 'partial-ok\n' > "$B/partial-config.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$B/partial-config.txt" || fail "existing root in partial config was not watched"
mkdir -p "$MISSING"
printf 'appeared-after-reload\n' > "$MISSING/reappeared.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$MISSING/reappeared.txt" || fail "missing root did not become watched after creation"

printf 'watch_dir=%s\nwatch_dir=%s/../B/\nwatch_dir=../watch/B/\n' "$B" "$B" > "$CONFIG"
before_lines="$(wc -l < "$CAPTURE")"
kill -HUP "$MONITOR_PID"
wait_for_record_since "$CAPTURE" "$before_lines" "facility=24" "duplicate directory ignored" || fail "normalized duplicate roots were not reported"
wait_for_record_since "$CAPTURE" "$before_lines" "facility=24" "configuration reloaded" || fail "duplicate-root configuration was not applied"
assert_pid_owned "$PID_FILE" "$MONITOR_PID" || fail "PID/process lost after duplicate-root reload"
printf 'duplicate-root-still-active\n' > "$B/duplicate-test.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$B/duplicate-test.txt" || fail "deduplicated root stopped working"

stop_monitor
[[ ! -e "$PID_FILE" ]] || fail "PID file remained after test shutdown"
echo "PASS: SIGHUP root replacement, nested watch removal, invalid/partial configs, normalized duplicates, PID ownership"
