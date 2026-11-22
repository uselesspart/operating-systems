#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/disk-monitor"
TMP_DIR="$(mktemp -d)"
FIRST_PID=""
SECOND_PID=""
DAEMON_PID=""

cleanup() {
  set +e
  for pid in "$FIRST_PID" "$SECOND_PID"; do
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      kill -TERM "$pid" 2>/dev/null
      wait "$pid" 2>/dev/null
    fi
  done
  if [[ -n "$DAEMON_PID" ]] && kill -0 "$DAEMON_PID" 2>/dev/null; then
    kill -TERM "$DAEMON_PID" 2>/dev/null
    if [[ -n "${DAEMON_PID_FILE:-}" ]]; then
      for _ in $(seq 1 100); do
        [[ ! -e "$DAEMON_PID_FILE" ]] && break
        sleep 0.05
      done
    fi
    for _ in $(seq 1 100); do
      [[ ! -e "/proc/${DAEMON_PID}/exe" ]] && break
      sleep 0.05
    done
  fi
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

wait_for_pid_file() {
  local pidfile="$1"
  for _ in $(seq 1 100); do
    [[ -s "$pidfile" ]] && return 0
    sleep 0.05
  done
  return 1
}

wait_for_pid_value() {
  local pidfile="$1"
  local expected="$2"
  for _ in $(seq 1 100); do
    if [[ -s "$pidfile" ]] && [[ "$(cat "$pidfile" 2>/dev/null || true)" == "$expected" ]]; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_pid_removal() {
  local pidfile="$1"
  for _ in $(seq 1 100); do
    [[ ! -e "$pidfile" ]] && return 0
    sleep 0.05
  done
  return 1
}

"${ROOT_DIR}/build.sh"
"$BIN" --help >/dev/null

echo "[1/8] Build and CLI lifecycle checks"

mkdir -p "$TMP_DIR/watch/root/child" "$TMP_DIR/watch/move-outside" "$TMP_DIR/configs"
printf 'watch_dir=%s\nwatch_dir=%s\n' "$TMP_DIR/watch/root" "$TMP_DIR/watch/root/child" > "$TMP_DIR/valid.conf"
printf '# comments and blank lines are accepted\n\nwatch_dir=../watch/root\nwatch_dir=../watch/root/child/..\n' > "$TMP_DIR/configs/active.conf"
PID_FILE="$TMP_DIR/monitor.pid"

"$BIN" --foreground --config "$TMP_DIR/configs/active.conf" --pid-file "$PID_FILE" &
FIRST_PID=$!
wait_for_pid_file "$PID_FILE" || fail "foreground monitor did not create its PID file"
[[ "$(cat "$PID_FILE")" == "$FIRST_PID" ]] || fail "PID file does not contain the foreground process PID"
kill -0 "$FIRST_PID" 2>/dev/null || fail "foreground monitor exited immediately"
echo "[2/8] Foreground startup and PID ownership"

printf 'initial\n' > "$TMP_DIR/watch/root/child/read-write.txt"
cat "$TMP_DIR/watch/root/child/read-write.txt" >/dev/null
printf 'changed\n' >> "$TMP_DIR/watch/root/child/read-write.txt"
mv "$TMP_DIR/watch/root/child/read-write.txt" "$TMP_DIR/watch/root/child/renamed.txt"
rm "$TMP_DIR/watch/root/child/renamed.txt"
mkdir "$TMP_DIR/watch/root/new-nested"
printf 'new-directory\n' > "$TMP_DIR/watch/root/new-nested/item.txt"
mv "$TMP_DIR/watch/root/new-nested" "$TMP_DIR/watch/move-outside/new-nested"
mv "$TMP_DIR/watch/move-outside/new-nested" "$TMP_DIR/watch/root/moved-back"

printf 'not_a_directive\n' > "$TMP_DIR/configs/active.conf"
kill -HUP "$FIRST_PID"
sleep 0.2
kill -0 "$FIRST_PID" 2>/dev/null || fail "monitor exited after malformed SIGHUP reload"
[[ -s "$PID_FILE" ]] || fail "PID file disappeared after rejected reload"

mkdir -p "$TMP_DIR/watch/alternate"
printf 'watch_dir=%s\n' "$TMP_DIR/watch/alternate" > "$TMP_DIR/configs/active.conf"
kill -HUP "$FIRST_PID"
sleep 0.2
kill -0 "$FIRST_PID" 2>/dev/null || fail "monitor exited after valid SIGHUP reload"
echo "[3/8] File operations, recursive directory changes, and SIGHUP stability"

"$BIN" --foreground --config "$TMP_DIR/configs/active.conf" --pid-file "$PID_FILE" &
SECOND_PID=$!
wait_for_pid_value "$PID_FILE" "$SECOND_PID" || fail "second instance did not replace the first PID"
wait "$FIRST_PID" || true
FIRST_PID=""
kill -0 "$SECOND_PID" 2>/dev/null || fail "replacement instance did not remain alive"
echo "[4/8] Duplicate-instance handling"

kill -TERM "$SECOND_PID"
wait "$SECOND_PID"
SECOND_PID=""
wait_for_pid_removal "$PID_FILE" || fail "PID file was not removed after SIGTERM"
echo "[5/8] SIGTERM shutdown and PID-file cleanup"

printf 'watch_dir=%s\nwatch_dir=%s\n' "$TMP_DIR/does-not-exist" "$TMP_DIR/watch/alternate" > "$TMP_DIR/configs/active.conf"
"$BIN" --foreground --config "$TMP_DIR/configs/active.conf" --pid-file "$PID_FILE" &
FIRST_PID=$!
wait_for_pid_file "$PID_FILE" || fail "monitor did not start with a valid root beside a missing root"
kill -TERM "$FIRST_PID"
wait "$FIRST_PID"
FIRST_PID=""
wait_for_pid_removal "$PID_FILE" || fail "PID file remained after partial-config test"

printf 'unknown_key=value\n' > "$TMP_DIR/bad.conf"
if "$BIN" --foreground --config "$TMP_DIR/bad.conf" --pid-file "$TMP_DIR/bad.pid" >/dev/null 2>&1; then
  fail "malformed initial configuration was accepted"
fi
[[ ! -e "$TMP_DIR/bad.pid" ]] || fail "PID file was created for a rejected initial configuration"
echo "[6/8] Missing directories and invalid initial configuration"

printf 'this-is-not-a-pid\n' > "$PID_FILE"
"$BIN" --foreground --config "$TMP_DIR/configs/active.conf" --pid-file "$PID_FILE" &
FIRST_PID=$!
wait_for_pid_value "$PID_FILE" "$FIRST_PID" || fail "damaged PID file was not safely replaced"
kill -TERM "$FIRST_PID"
wait "$FIRST_PID"
FIRST_PID=""
wait_for_pid_removal "$PID_FILE" || fail "PID cleanup failed after damaged-file test"
printf '%s\n' "$$" > "$PID_FILE"
"$BIN" --foreground --config "$TMP_DIR/configs/active.conf" --pid-file "$PID_FILE" &
FIRST_PID=$!
wait_for_pid_value "$PID_FILE" "$FIRST_PID" || fail "foreign PID file was not safely replaced"
kill -0 "$$" 2>/dev/null || fail "test shell was signaled because its PID was in the file"
kill -TERM "$FIRST_PID"
wait "$FIRST_PID"
FIRST_PID=""
wait_for_pid_removal "$PID_FILE" || fail "PID cleanup failed after foreign-PID test"
echo "[7/8] Damaged and foreign PID safety checks"

DAEMON_CONFIG="$TMP_DIR/daemon.conf"
DAEMON_PID_FILE="$TMP_DIR/daemonized.pid"
printf 'watch_dir=%s\n' "$TMP_DIR/watch/alternate" > "$DAEMON_CONFIG"
"$BIN" --config "$DAEMON_CONFIG" --pid-file "$DAEMON_PID_FILE" &
LAUNCHER_PID=$!
wait "$LAUNCHER_PID" || fail "daemon launcher reported startup failure"
wait_for_pid_file "$DAEMON_PID_FILE" || fail "daemonized launch returned without creating a PID file"
DAEMON_PID="$(cat "$DAEMON_PID_FILE")"
[[ "$DAEMON_PID" != "$LAUNCHER_PID" ]] || fail "PID file contains the short-lived launcher instead of the daemon child"
kill -0 "$DAEMON_PID" 2>/dev/null || fail "daemonized child is not running"
kill -TERM "$DAEMON_PID"
wait_for_pid_removal "$DAEMON_PID_FILE" || fail "daemonized process did not remove its PID file"
for _ in $(seq 1 100); do
  [[ ! -e "/proc/${DAEMON_PID}/exe" ]] && break
  sleep 0.05
done
[[ ! -e "/proc/${DAEMON_PID}/exe" ]] || fail "daemon process remained alive after PID-file removal"
DAEMON_PID=""
echo "[8/8] PASS: double-fork daemon startup and shutdown completed." 

echo "PASS: lifecycle, PID ownership, reload stability, and daemonization checks completed."
