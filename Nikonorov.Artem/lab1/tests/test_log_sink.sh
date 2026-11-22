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
mkdir "$WATCH"
SINK="$WATCH/nonstandard-disk-monitor.log"
: > "$SINK"
CONFIG="$TMP_DIR/monitor.conf"
PID_FILE="$TMP_DIR/monitor.pid"
OUTPUT="$TMP_DIR/monitor.out"
printf 'watch_dir=%s\nexclude_path=%s\n' "$WATCH" "$SINK" > "$CONFIG"

start_monitor "$CONFIG" "$PID_FILE" "$SINK" "$OUTPUT"
sleep 1
COUNT_BEFORE="$(wc -l < "$SINK")"
sleep 3
COUNT_AFTER="$(wc -l < "$SINK")"
[[ "$COUNT_BEFORE" -eq "$COUNT_AFTER" ]] || {
  echo "FAIL: excluded sink grew during idle ($COUNT_BEFORE -> $COUNT_AFTER lines)" >&2
  tail -n 30 "$SINK" >&2
  exit 1
}

mv "$SINK" "$SINK.1"
: > "$SINK"
printf 'rotate-content\n' >> "$SINK.1"
printf 'new-active-content\n' >> "$SINK"
sleep 0.5
if record_exists "$SINK" "facility=128" "path=$SINK" ||
   record_exists "$SINK" "facility=128" "path=$SINK.1" ||
   record_exists "$SINK.1" "facility=128" "path=$SINK" ||
   record_exists "$SINK.1" "facility=128" "path=$SINK.1"; then
  fail "events from configured log sink or its rotation were recorded"
fi
COUNT_ROTATED="$(wc -l < "$SINK")"
sleep 2
COUNT_STABLE="$(wc -l < "$SINK")"
[[ "$COUNT_ROTATED" -eq "$COUNT_STABLE" ]] || fail "sink feedback loop appeared after rotation"

assert_record "$SINK.1" "facility=24" "disk-monitor started" || fail "service logs missing from captured sink"
stop_monitor
[[ ! -e "$PID_FILE" ]] || fail "PID file remained after sink test"
echo "PASS: custom exclude_path and rotation; quiet sink lines=$COUNT_BEFORE->$COUNT_AFTER over 3 seconds; post-rotation lines=$COUNT_ROTATED->$COUNT_STABLE over final 2 seconds (0 added)"
