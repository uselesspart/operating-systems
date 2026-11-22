#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/disk-monitor"
source "${ROOT_DIR}/tests/support/common.sh"
ensure_capture_library

TMP_DIR="$(mktemp -d)"
MONITOR_PID=""
ORIGINAL_QUEUE=""
QUEUE_CHANGED=0

restore_queue() {
  if [[ "$QUEUE_CHANGED" -ne 1 || -z "$ORIGINAL_QUEUE" ]]; then
    return 0
  fi

  if ! sysctl -w "fs.inotify.max_queued_events=$ORIGINAL_QUEUE" >/dev/null 2>&1; then
    echo "FAIL: could not restore fs.inotify.max_queued_events to $ORIGINAL_QUEUE." >&2
    return 1
  fi

  local restored_queue
  restored_queue="$(sysctl -n fs.inotify.max_queued_events 2>/dev/null || true)"
  if [[ "$restored_queue" != "$ORIGINAL_QUEUE" ]]; then
    echo "FAIL: queue limit restoration could not be verified; expected $ORIGINAL_QUEUE, got ${restored_queue:-unknown}." >&2
    return 1
  fi

  QUEUE_CHANGED=0
  return 0
}

cleanup() {
  local exit_code=$?
  trap - EXIT

  if [[ -n "$MONITOR_PID" ]]; then
    kill -CONT "$MONITOR_PID" 2>/dev/null || true
    stop_monitor || true
    MONITOR_PID=""
  fi

  # Best-effort fallback for failures before the explicit restoration step.
  if [[ "$QUEUE_CHANGED" -eq 1 && -n "$ORIGINAL_QUEUE" ]]; then
    if ! restore_queue; then
      echo "WARNING: could not verify restoration of fs.inotify.max_queued_events." >&2
      [[ "$exit_code" -ne 0 ]] || exit_code=1
    fi
  fi

  rm -rf -- "$TMP_DIR"
  cleanup_capture_library
  exit "$exit_code"
}
trap cleanup EXIT

if [[ "$EUID" -ne 0 ]] || ! command -v sysctl >/dev/null 2>&1; then
  echo "SKIP: live inotify overflow requires root and sysctl access."
  exit 77
fi

ORIGINAL_QUEUE="$(sysctl -n fs.inotify.max_queued_events 2>/dev/null || true)"
if [[ -z "$ORIGINAL_QUEUE" ]]; then
  echo "SKIP: cannot read fs.inotify.max_queued_events."
  exit 77
fi

if ! sysctl -w fs.inotify.max_queued_events=64 >/dev/null 2>&1; then
  echo "SKIP: kernel/container does not permit changing fs.inotify.max_queued_events."
  exit 77
fi
QUEUE_CHANGED=1

WATCH="$TMP_DIR/watch"
mkdir "$WATCH"
CONFIG="$TMP_DIR/monitor.conf"
PID_FILE="$TMP_DIR/monitor.pid"
CAPTURE="$TMP_DIR/captured.log"
OUTPUT="$TMP_DIR/monitor.out"
: > "$CAPTURE"
printf 'watch_dir=%s\n' "$WATCH" > "$CONFIG"

start_monitor "$CONFIG" "$PID_FILE" "$CAPTURE" "$OUTPUT"

# Pause the event loop so the intentionally small kernel queue can overflow.
kill -STOP "$MONITOR_PID"
python3 - "$WATCH" <<'PY'
import os
import sys

root = sys.argv[1]
for index in range(3000):
    path = os.path.join(root, f"overflow-{index}.txt")
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    try:
        os.write(descriptor, b"overflow\n")
    finally:
        os.close(descriptor)
PY
kill -CONT "$MONITOR_PID"

if ! wait_for_record "$CAPTURE" "facility=24" "inotify queue overflow detected"; then
  echo "FAIL: queue was reduced and saturated but the overflow handler record was not found." >&2
  tail -n 40 "$CAPTURE" >&2 || true
  cat "$OUTPUT" >&2 || true
  exit 1
fi

sleep 1
RECOVERY_DIR="$WATCH/recovered-after-overflow"
if ! mkdir "$RECOVERY_DIR"; then
  echo "FAIL: could not create the post-overflow recovery directory." >&2
  exit 1
fi

if ! wait_for_record "$CAPTURE" "facility=128" "event=CREATE" "object_type=directory" "path=$RECOVERY_DIR"; then
  echo "FAIL: directory creation was not logged after overflow recovery." >&2
  tail -n 40 "$CAPTURE" >&2 || true
  cat "$OUTPUT" >&2 || true
  exit 1
fi

sleep 0.2
RECOVERY_FILE="$RECOVERY_DIR/post-overflow.txt"
printf 'post-overflow\n' > "$RECOVERY_FILE"

if ! wait_for_record "$CAPTURE" "facility=128" "event=CREATE" "object_type=file" "path=$RECOVERY_FILE"; then
  echo "FAIL: the new nested directory was not monitored after overflow recovery." >&2
  tail -n 40 "$CAPTURE" >&2 || true
  cat "$OUTPUT" >&2 || true
  exit 1
fi

stop_monitor
MONITOR_PID=""

if ! restore_queue; then
  exit 1
fi

echo "PASS: live IN_Q_OVERFLOW was triggered; post-overflow directory and file events were detected; original queue limit $ORIGINAL_QUEUE restored and verified."