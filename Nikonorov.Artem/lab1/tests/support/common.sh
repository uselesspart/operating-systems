#!/usr/bin/env bash

ensure_capture_library() {
  if [[ -n "${DISKMON_CAPTURE_LIB:-}" && -f "${DISKMON_CAPTURE_LIB}" ]]; then
    CAPTURE_SO="$DISKMON_CAPTURE_LIB"
    return
  fi
  CAPTURE_TMP_DIR="$(mktemp -d)"
  CAPTURE_SO="${CAPTURE_TMP_DIR}/libsyslog_capture.so"
  gcc -shared -fPIC -Wall -Wextra -Werror -pedantic \
    "${ROOT_DIR}/tests/support/syslog_capture.c" -o "$CAPTURE_SO"
}

cleanup_capture_library() {
  if [[ -n "${CAPTURE_TMP_DIR:-}" && -d "$CAPTURE_TMP_DIR" ]]; then
    rm -rf -- "$CAPTURE_TMP_DIR"
  fi
}

wait_for_pid_file() {
  local pidfile="$1"
  for _ in $(seq 1 100); do
    [[ -s "$pidfile" ]] && return 0
    sleep 0.05
  done
  return 1
}

record_exists() {
  local file="$1"
  shift
  [[ -f "$file" ]] || return 1
  local line token
  while IFS= read -r line; do
    for token in "$@"; do
      [[ "$line" == *"$token"* ]] || continue 2
    done
    return 0
  done < "$file"
  return 1
}

record_exists_since() {
  local file="$1" start_line="$2"
  shift 2
  [[ -f "$file" ]] || return 1
  local line line_number=0 token
  while IFS= read -r line; do
    line_number=$((line_number + 1))
    (( line_number > start_line )) || continue
    for token in "$@"; do
      [[ "$line" == *"$token"* ]] || continue 2
    done
    return 0
  done < "$file"
  return 1
}

wait_for_record_since() {
  local file="$1" start_line="$2"
  shift 2
  for _ in $(seq 1 100); do
    if record_exists_since "$file" "$start_line" "$@"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

wait_for_record() {
  local file="$1"
  shift
  for _ in $(seq 1 100); do
    if record_exists "$file" "$@"; then
      return 0
    fi
    sleep 0.05
  done
  return 1
}

assert_record() {
  local file="$1"
  shift
  if ! record_exists "$file" "$@"; then
    echo "FAIL: expected captured syslog record containing: $*" >&2
    echo "Captured records (last 40):" >&2
    tail -n 40 "$file" >&2 2>/dev/null || true
    return 1
  fi
}

assert_no_record() {
  local file="$1"
  shift
  if record_exists "$file" "$@"; then
    echo "FAIL: unexpected captured syslog record containing: $*" >&2
    grep -F "$2" "$file" | tail -n 10 >&2 || true
    return 1
  fi
}

start_monitor() {
  local config="$1" pidfile="$2" capture="$3" output="$4"
  DISKMON_TEST_LOG="$capture" LD_PRELOAD="$CAPTURE_SO" \
    "$BIN" --foreground --config "$config" --pid-file "$pidfile" >"$output" 2>&1 &
  MONITOR_PID=$!
  if ! wait_for_pid_file "$pidfile"; then
    echo "FAIL: monitor did not create PID file; output:" >&2
    cat "$output" >&2 || true
    return 1
  fi
  kill -0 "$MONITOR_PID" 2>/dev/null || {
    echo "FAIL: monitor exited during startup" >&2
    cat "$output" >&2 || true
    return 1
  }
  sleep 0.2
}

stop_monitor() {
  if [[ -n "${MONITOR_PID:-}" ]] && kill -0 "$MONITOR_PID" 2>/dev/null; then
    kill -TERM "$MONITOR_PID" 2>/dev/null || true
    wait "$MONITOR_PID" 2>/dev/null || true
  fi
  MONITOR_PID=""
}

assert_pid_owned() {
  local pidfile="$1" pid="$2"
  [[ -s "$pidfile" ]] || { echo "FAIL: PID file disappeared: $pidfile" >&2; return 1; }
  [[ "$(cat "$pidfile")" == "$pid" ]] || {
    echo "FAIL: PID file content changed unexpectedly: $pidfile" >&2
    return 1
  }
  kill -0 "$pid" 2>/dev/null || { echo "FAIL: daemon process $pid is not alive" >&2; return 1; }
}
