#!/usr/bin/env bash
set -uo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${ROOT_DIR}/disk-monitor"
TMP_DIR="$(mktemp -d)"
RESULTS_FILE="$TMP_DIR/results.tsv"
HAD_BINARY=0
[[ -e "$BIN" ]] && HAD_BINARY=1

cleanup() {
  rm -rf -- "$TMP_DIR"
  if [[ "$HAD_BINARY" -eq 0 ]]; then
    rm -f -- "$BIN"
  fi
}
trap cleanup EXIT

printf 'test\tstatus\tdetail\n' > "$RESULTS_FILE"
FAILURES=0

record_result() {
  local name="$1" status="$2" detail="$3"
  printf '%s\t%s\t%s\n' "$name" "$status" "$detail" >> "$RESULTS_FILE"
  printf '[%s] %s: %s\n' "$status" "$name" "$detail"
  [[ "$status" != "FAIL" ]] || FAILURES=$((FAILURES + 1))
}

run_one() {
  local name="$1" script="$2" rc=0 status detail
  local output="$TMP_DIR/${name}.out"
  local timeout_seconds="${DISKMON_TEST_TIMEOUT:-45}"
  echo "--- running $name (timeout=${timeout_seconds}s) ---"
  if timeout --signal=TERM --kill-after=3s "${timeout_seconds}s" "$script" >"$output" 2>&1; then
    status="PASS"
    detail="$(tail -n 1 "$output" | tr '\t\n' '  ' | sed 's/[[:space:]]\+/ /g')"
  else
    rc=$?
    if [[ "$rc" -eq 77 ]]; then
      status="SKIP"
      detail="$(tail -n 1 "$output" | tr '\t\n' '  ' | sed 's/[[:space:]]\+/ /g')"
    elif [[ "$rc" -eq 124 || "$rc" -eq 137 || "$rc" -eq 143 ]]; then
      status="FAIL"
      detail="timed out after ${timeout_seconds}s; $(tail -n 3 "$output" | tr '\t\n' '  ' | sed 's/[[:space:]]\+/ /g')"
    else
      status="FAIL"
      detail="exit=$rc; $(tail -n 3 "$output" | tr '\t\n' '  ' | sed 's/[[:space:]]\+/ /g')"
    fi
  fi
  cat "$output"
  record_result "$name" "$status" "$detail"
}

echo "Building with the repository's strict build.sh flags..."
if ! "$ROOT_DIR/build.sh"; then
  record_result "build" "FAIL" "strict build.sh failed"
  echo "\nFinal results:"
  column -t -s $'\t' "$RESULTS_FILE" 2>/dev/null || cat "$RESULTS_FILE"
  exit 1
fi
record_result "build" "PASS" "g++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2"

export DISKMON_CAPTURE_LIB="$TMP_DIR/libsyslog_capture.so"
if gcc -shared -fPIC -Wall -Wextra -Werror -pedantic \
    "$ROOT_DIR/tests/support/syslog_capture.c" -o "$DISKMON_CAPTURE_LIB"; then
  record_result "syslog-capture-build" "PASS" "LD_PRELOAD syslog/openlog/closelog/__syslog_chk interposer built"
else
  record_result "syslog-capture-build" "FAIL" "LD_PRELOAD capture library failed to compile"
  exit 1
fi

run_one "lifecycle" "$ROOT_DIR/tests/test_lifecycle.sh"
run_one "filesystem-events-and-idle" "$ROOT_DIR/tests/test_events.sh"
run_one "sighup-and-config" "$ROOT_DIR/tests/test_reload.sh"
run_one "root-recovery-and-default-config" "$ROOT_DIR/tests/test_root_recovery.sh"
run_one "pid-path-and-early-cli-safety" "$ROOT_DIR/tests/test_process_safety.sh"
run_one "symlink-and-log-field-safety" "$ROOT_DIR/tests/test_path_and_log_fields.sh"
run_one "log-sink-loop-protection" "$ROOT_DIR/tests/test_log_sink.sh"
run_one "overflow-unit-policy" "$ROOT_DIR/tests/test_overflow_unit.sh"
run_one "overflow-live-kernel" "$ROOT_DIR/tests/test_overflow_live.sh"
run_one "real-rsyslog-e2e" "$ROOT_DIR/tests/test_rsyslog.sh"

echo
echo "Final results:"
if command -v column >/dev/null 2>&1; then
  column -t -s $'\t' "$RESULTS_FILE"
else
  cat "$RESULTS_FILE"
fi
if [[ "$FAILURES" -ne 0 ]]; then
  echo "FAIL: $FAILURES test(s) failed." >&2
  exit 1
fi
echo "No test failures. SKIP means the live check's prerequisites were unavailable."
