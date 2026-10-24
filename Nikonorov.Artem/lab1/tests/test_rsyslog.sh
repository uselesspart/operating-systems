#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/disk-monitor"
LOG_FILE="/var/log/disk_monitor.log"
CONFIG_FILE="/etc/rsyslog.d/30-disk-monitor.conf"

skip() { echo "SKIP: $*"; exit 77; }
fail() { echo "FAIL: $*" >&2; exit 1; }

command -v rsyslogd >/dev/null 2>&1 || skip "rsyslogd is not installed."
[[ -S /dev/log ]] || skip "/dev/log is not a Unix socket; real syslog delivery is unavailable."
[[ -r "$CONFIG_FILE" ]] || skip "$CONFIG_FILE is not installed; this test does not change system configuration."
[[ -r "$LOG_FILE" ]] || skip "$LOG_FILE is not readable by the current user."
command -v logger >/dev/null 2>&1 || skip "logger utility is unavailable."

if ! rsyslogd -N1; then
  fail "rsyslogd -N1 rejected the installed configuration."
fi

TMP_DIR="$(mktemp -d)"
PID_FILE="$TMP_DIR/monitor.pid"
CONFIG="$TMP_DIR/monitor.conf"
OUTPUT="$TMP_DIR/monitor.out"
WATCH="$TMP_DIR/watch"
MONITOR_PID=""
TOKEN="disk-monitor-e2e-$(date +%s)-$$"
cleanup() {
  set +e
  if [[ -n "$MONITOR_PID" ]] && kill -0 "$MONITOR_PID" 2>/dev/null; then
    kill -TERM "$MONITOR_PID" 2>/dev/null
    wait "$MONITOR_PID" 2>/dev/null
  fi
  rm -rf -- "$TMP_DIR"
}
trap cleanup EXIT

mkdir "$WATCH"
printf 'watch_dir=%s\nexclude_path=%s\n' "$WATCH" "$LOG_FILE" > "$CONFIG"
BEFORE="$(wc -l < "$LOG_FILE")"
"$BIN" --foreground --config "$CONFIG" --pid-file "$PID_FILE" >"$OUTPUT" 2>&1 &
MONITOR_PID=$!
for _ in $(seq 1 100); do
  [[ -s "$PID_FILE" ]] && break
  sleep 0.05
done
[[ -s "$PID_FILE" ]] || fail "monitor did not start; output: $(cat "$OUTPUT")"

printf 'real-rsyslog\n' > "$WATCH/$TOKEN.txt"
cat "$WATCH/$TOKEN.txt" >/dev/null
found=0
for _ in $(seq 1 100); do
  if tail -n "+$((BEFORE + 1))" "$LOG_FILE" | grep -F "$TOKEN.txt" >/dev/null; then
    found=1
    break
  fi
  sleep 0.1
done
[[ "$found" -eq 1 ]] || fail "disk-monitor's LOG_LOCAL0 event did not reach $LOG_FILE"

FOREIGN="unrelated-local0-$TOKEN"
logger -p local0.info -t unrelated-test "$FOREIGN"
DAEMON_ONLY="daemon-facility-$TOKEN"
logger -p daemon.info -t disk-monitor "$DAEMON_ONLY"
sleep 0.5
if tail -n "+$((BEFORE + 1))" "$LOG_FILE" | grep -F "$FOREIGN" >/dev/null; then
  fail "unrelated program's LOG_LOCAL0 message was routed into $LOG_FILE"
fi
if tail -n "+$((BEFORE + 1))" "$LOG_FILE" | grep -F "$DAEMON_ONLY" >/dev/null; then
  fail "LOG_DAEMON message was routed into $LOG_FILE"
fi

kill -TERM "$MONITOR_PID"
wait "$MONITOR_PID"
MONITOR_PID=""
echo "PASS: real rsyslog LOG_LOCAL0 routing, narrow program/facility filter, and LOG_DAEMON/unrelated-program exclusions"
