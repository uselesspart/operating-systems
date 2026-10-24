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

WATCH="${TMP_DIR}/watch"
OUTSIDE="${TMP_DIR}/outside"
mkdir -p "$WATCH/existing/nested" "$OUTSIDE"
CAPTURE="${TMP_DIR}/captured.log"
CONFIG="${TMP_DIR}/monitor.conf"
PID_FILE="${TMP_DIR}/monitor.pid"
OUTPUT="${TMP_DIR}/monitor.out"
: > "$CAPTURE"
printf 'watch_dir=%s\n' "$WATCH" > "$CONFIG"

start_monitor "$CONFIG" "$PID_FILE" "$CAPTURE" "$OUTPUT"
assert_record "$CAPTURE" "facility=24" "ident=disk-monitor" "disk-monitor started" || fail "service logs are not using LOG_DAEMON"

# File operations: verify specific paths, rather than only checking liveness.
TYPE_FILE="$WATCH/types.txt"
: > "$TYPE_FILE"
wait_for_record "$CAPTURE" "facility=128" "event=CREATE" "object_type=file" "path=$TYPE_FILE" || fail "CREATE record missing or object_type incorrect"
printf 'write-content\n' >> "$TYPE_FILE"
wait_for_record "$CAPTURE" "facility=128" "event=OPEN" "path=$TYPE_FILE" || fail "OPEN record missing"
wait_for_record "$CAPTURE" "facility=128" "event=MODIFY" "path=$TYPE_FILE" || fail "MODIFY record missing"
wait_for_record "$CAPTURE" "facility=128" "event=CLOSE_WRITE" "path=$TYPE_FILE" || fail "CLOSE_WRITE record missing"
cat "$TYPE_FILE" >/dev/null
wait_for_record "$CAPTURE" "facility=128" "event=ACCESS" "path=$TYPE_FILE" || fail "ACCESS record missing"
wait_for_record "$CAPTURE" "facility=128" "event=CLOSE_NOWRITE" "path=$TYPE_FILE" || fail "CLOSE_NOWRITE record missing"
chmod 600 "$TYPE_FILE"
wait_for_record "$CAPTURE" "facility=128" "event=ATTRIB" "path=$TYPE_FILE" || fail "ATTRIB record missing"
RENAMED_FILE="$WATCH/renamed.txt"
mv "$TYPE_FILE" "$RENAMED_FILE"
wait_for_record "$CAPTURE" "facility=128" "event=MOVED_FROM" "path=$TYPE_FILE" || fail "MOVED_FROM record/path missing"
wait_for_record "$CAPTURE" "facility=128" "event=MOVED_TO" "path=$RENAMED_FILE" || fail "MOVED_TO record/path missing"
rm "$RENAMED_FILE"
wait_for_record "$CAPTURE" "facility=128" "event=DELETE" "path=$RENAMED_FILE" || fail "DELETE record missing"

# Events in subdirectories that existed before the monitor started.
NESTED_FILE="$WATCH/existing/nested/nested-file.txt"
printf 'nested\n' > "$NESTED_FILE"
wait_for_record "$CAPTURE" "facility=128" "event=CREATE" "path=$NESTED_FILE" || fail "existing nested directory was not watched"

# Rapidly create deep trees before the daemon necessarily adds each watch.
# The scan-after-watch path must discover every file, natively or synthetically.
for index in $(seq 1 20); do
  race_dir="$WATCH/race-$index/a/b"
  race_file="$race_dir/file-$index.txt"
  mkdir -p "$race_dir"
  printf 'fast-%s\n' "$index" > "$race_file"
done
for index in $(seq 1 20); do
  race_file="$WATCH/race-$index/a/b/file-$index.txt"
  wait_for_record "$CAPTURE" "facility=128" "path=$race_file" || fail "fast nested creation was lost: $race_file"
done

# A directory moved into the tree is recursively watched and pre-existing
# contents are identified as synthetic discoveries.
mkdir -p "$OUTSIDE/incoming/deep"
printf 'pre-existing\n' > "$OUTSIDE/incoming/deep/held.txt"
mv "$OUTSIDE/incoming" "$WATCH/moved-in"
wait_for_record "$CAPTURE" "facility=128" "event=MOVED_TO" "object_type=directory" "path=$WATCH/moved-in" || fail "MOVED_TO for incoming directory missing or object_type incorrect"
wait_for_record "$CAPTURE" "facility=128" "source=synthetic" "event=DISCOVERED" "path=$WATCH/moved-in/deep/held.txt" || fail "moved-in subtree was not synthetically discovered"
printf 'new-event\n' > "$WATCH/moved-in/deep/after-move.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/moved-in/deep/after-move.txt" || fail "moved-in descendant was not watched"

# Directory move-out removes descendant watches: later writes outside must be silent.
mkdir -p "$WATCH/move-out/deep"
printf 'before\n' > "$WATCH/move-out/deep/before.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/move-out/deep/before.txt" || fail "move-out setup file was not watched"
mv "$WATCH/move-out" "$OUTSIDE/move-out"
sleep 0.5
printf 'must-not-log\n' > "$OUTSIDE/move-out/deep/late-outside.txt"
sleep 0.5
if record_exists "$CAPTURE" "path=$OUTSIDE/move-out/deep/late-outside.txt"; then
  fail "events from a directory moved outside the watched tree were still logged"
fi

# Rename a watched directory, then recreate its old pathname with a different inode.
mkdir -p "$WATCH/path-A/deep"
printf 'before-rename\n' > "$WATCH/path-A/deep/before.txt"
mv "$WATCH/path-A" "$WATCH/path-B"
wait_for_record "$CAPTURE" "facility=128" "event=MOVED_TO" "path=$WATCH/path-B" || fail "directory rename-to path missing"
mkdir -p "$WATCH/path-A/deep"
printf 'new-inode\n' > "$WATCH/path-A/deep/new-instance.txt"
printf 'renamed-inode\n' > "$WATCH/path-B/deep/renamed-instance.txt"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/path-A/deep/new-instance.txt" || fail "replacement directory at former path was not watched"
wait_for_record "$CAPTURE" "facility=128" "path=$WATCH/path-B/deep/renamed-instance.txt" || fail "renamed directory watch mapping was lost or confused"

# Confirm facilities are separated: service control messages go to LOG_DAEMON,
# filesystem events go to LOG_LOCAL0.
assert_record "$CAPTURE" "facility=128" "event=CREATE" "path=$NESTED_FILE" || fail "filesystem event facility was not LOG_LOCAL0"

stop_monitor
[[ ! -e "$PID_FILE" ]] || fail "PID file was not removed on SIGTERM"

# Ten quiet seconds in a clean watched root must add exactly zero LOG_LOCAL0 rows.
IDLE_WATCH="$TMP_DIR/idle-watch"
mkdir "$IDLE_WATCH"
IDLE_CAPTURE="$TMP_DIR/idle-captured.log"
IDLE_CONFIG="$TMP_DIR/idle.conf"
IDLE_PID="$TMP_DIR/idle.pid"
IDLE_OUTPUT="$TMP_DIR/idle.out"
: > "$IDLE_CAPTURE"
printf 'watch_dir=%s\n' "$IDLE_WATCH" > "$IDLE_CONFIG"
start_monitor "$IDLE_CONFIG" "$IDLE_PID" "$IDLE_CAPTURE" "$IDLE_OUTPUT"
sleep 0.3
LOCAL0_BEFORE="$(grep -c '^facility=128 ' "$IDLE_CAPTURE" || true)"
sleep 10
LOCAL0_AFTER="$(grep -c '^facility=128 ' "$IDLE_CAPTURE" || true)"
[[ "$LOCAL0_BEFORE" -eq 0 && "$LOCAL0_AFTER" -eq 0 ]] || {
  echo "FAIL: idle test wrote LOG_LOCAL0 records (before=$LOCAL0_BEFORE after=$LOCAL0_AFTER)" >&2
  cat "$IDLE_CAPTURE" >&2
  exit 1
}
assert_record "$IDLE_CAPTURE" "facility=24" "disk-monitor started" || fail "service startup was not logged to LOG_DAEMON"
stop_monitor
[[ ! -e "$IDLE_PID" ]] || fail "idle PID file was not removed"

echo "PASS: event contents, nested/moved trees, 20 rapid creations, facility split; idle LOG_LOCAL0 rows before=$LOCAL0_BEFORE after=$LOCAL0_AFTER over 10 seconds"
