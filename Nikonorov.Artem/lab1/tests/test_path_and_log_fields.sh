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
OUTSIDE="$TMP_DIR/outside"
mkdir -p "$WATCH" "$OUTSIDE/secret"
printf 'existing secret\n' > "$OUTSIDE/secret/secret-before-start.txt"
ln -s "$OUTSIDE/secret" "$WATCH/link-to-outside"
CONFIG="$TMP_DIR/monitor.conf"
PID_FILE="$TMP_DIR/monitor.pid"
CAPTURE="$TMP_DIR/captured.log"
OUTPUT="$TMP_DIR/monitor.out"
: > "$CAPTURE"
printf 'watch_dir=%s\n' "$WATCH" > "$CONFIG"

start_monitor "$CONFIG" "$PID_FILE" "$CAPTURE" "$OUTPUT"

sleep 0.2
if record_exists "$CAPTURE" "path=$WATCH/link-to-outside/secret-before-start.txt"; then
  fail "monitor traversed a symbolic-link directory during initial scan"
fi
printf 'created after startup\n' > "$OUTSIDE/secret/secret-after-start.txt"
sleep 0.4
if record_exists "$CAPTURE" "path=$WATCH/link-to-outside/secret-after-start.txt"; then
  fail "monitor followed a symbolic-link directory after startup"
fi

SAFE_NAME='name with=equals.txt'
SAFE_FILE="$WATCH/$SAFE_NAME"
printf 'safe fields\n' > "$SAFE_FILE"
ENCODED_PATH="$WATCH/name\\x20with\\x3dequals.txt"
wait_for_record "$CAPTURE" "facility=128" "event=CREATE" "path=$ENCODED_PATH" \
  || fail "space/equal characters in path were not encoded unambiguously"

INJECTION_NAME=$'evil\nevent=DELETE path=forged-record'
INJECTION_FILE="$WATCH/$INJECTION_NAME"
printf 'do not parse this as log fields\n' > "$INJECTION_FILE"
ENCODED_INJECTION="$WATCH/evil\\nevent\\x3dDELETE\\x20path\\x3dforged-record"
wait_for_record "$CAPTURE" "facility=128" "path=$ENCODED_INJECTION" \
  || fail "newline/field separators in a filename were not escaped"
if record_exists "$CAPTURE" "path=forged-record"; then
  fail "a crafted file name forged a separate path field"
fi

stop_monitor
[[ ! -e "$PID_FILE" ]] || fail "PID file remained after path/log-field test"

ROOT_LINK="$TMP_DIR/root-symlink"
ln -s "$OUTSIDE/secret" "$ROOT_LINK"
ROOT_LINK_CONFIG="$TMP_DIR/root-link.conf"
ROOT_LINK_PID="$TMP_DIR/root-link.pid"
ROOT_LINK_CAPTURE="$TMP_DIR/root-link-captured.log"
ROOT_LINK_OUTPUT="$TMP_DIR/root-link.out"
: > "$ROOT_LINK_CAPTURE"
printf 'watch_dir=%s\n' "$ROOT_LINK" > "$ROOT_LINK_CONFIG"
start_monitor "$ROOT_LINK_CONFIG" "$ROOT_LINK_PID" "$ROOT_LINK_CAPTURE" "$ROOT_LINK_OUTPUT"
wait_for_record "$ROOT_LINK_CAPTURE" "facility=24" "watch root is a symbolic link and will not be traversed" \
  || fail "configured symbolic-link root was not explicitly diagnosed"
printf 'root symlink target event\n' > "$OUTSIDE/secret/secret-root-link.txt"
sleep 0.4
if record_exists "$ROOT_LINK_CAPTURE" "path=$ROOT_LINK/secret-root-link.txt"; then
  fail "configured symbolic-link root was traversed"
fi
stop_monitor
[[ ! -e "$ROOT_LINK_PID" ]] || fail "PID file remained after symbolic-link-root test"
echo "PASS: nested and configured symlink directories are not traversed; event fields escape spaces, '=', and newlines"
