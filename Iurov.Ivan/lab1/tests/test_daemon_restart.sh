#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="${SCRIPT_DIR}/build/treecleaner"
TMP_DIR="$(mktemp -d /tmp/treecleaner-restart-test.XXXXXX)"
PID_FILE="${TMP_DIR}/treecleaner.pid"
LOG_FILE="${TMP_DIR}/treecleaner.log"
CONFIG_FILE="${TMP_DIR}/cleaner.conf"
DAEMON_PIDS=()

cleanup() {
  if [[ -f "${PID_FILE}" ]]; then
    file_pid="$(cat "${PID_FILE}" 2>/dev/null || true)"
    if [[ "${file_pid}" =~ ^[0-9]+$ ]]; then
      DAEMON_PIDS+=("${file_pid}")
    fi
  fi
  for daemon_pid in "${DAEMON_PIDS[@]}"; do
    if [[ "${daemon_pid}" =~ ^[0-9]+$ ]] && [[ -d "/proc/${daemon_pid}" ]] &&
       tr '\0' ' ' < "/proc/${daemon_pid}/cmdline" 2>/dev/null | grep -q -- '--daemon'; then
      kill -TERM "${daemon_pid}" 2>/dev/null || true
    fi
  done
  for daemon_pid in "${DAEMON_PIDS[@]}"; do
    for _ in $(seq 1 40); do
      [[ ! -d "/proc/${daemon_pid}" ]] && break
      sleep 0.05
    done
    if [[ -d "/proc/${daemon_pid}" ]] &&
       tr '\0' ' ' < "/proc/${daemon_pid}/cmdline" 2>/dev/null | grep -q -- '--daemon'; then
      kill -KILL "${daemon_pid}" 2>/dev/null || true
    fi
  done
  rm -rf -- "${TMP_DIR}"
}
trap cleanup EXIT

mkdir -p "${TMP_DIR}/root/branch/target"
printf 'temporary test file\n' > "${TMP_DIR}/root/branch/target/file.txt"
printf '%s 2\n' "${TMP_DIR}/root" > "${CONFIG_FILE}"

start_daemon() {
  "${BIN}" --config "${CONFIG_FILE}" --daemon --interval 60 \
    --log-file "${LOG_FILE}" --pid-file "${PID_FILE}" >/dev/null
}

wait_for_pid() {
  local previous="${1:-}"
  for _ in $(seq 1 100); do
    if [[ -s "${PID_FILE}" ]]; then
      local current
      current="$(cat "${PID_FILE}" 2>/dev/null || true)"
      if [[ "${current}" =~ ^[0-9]+$ ]] && [[ "${current}" != "${previous}" ]] && [[ -d "/proc/${current}" ]]; then
        printf '%s\n' "${current}"
        return 0
      fi
    fi
    sleep 0.05
  done
  echo "Timed out waiting for daemon PID; log follows:" >&2
  cat "${LOG_FILE}" >&2 || true
  return 1
}

start_daemon
first_pid="$(wait_for_pid)"
DAEMON_PIDS+=("${first_pid}")
kill -0 "${first_pid}"

# Starting the same daemon again must terminate and replace the first instance.
start_daemon
second_pid="$(wait_for_pid "${first_pid}")"
DAEMON_PIDS+=("${second_pid}")
if [[ "${first_pid}" == "${second_pid}" ]]; then
  echo "FAIL: restart reused the old PID" >&2
  exit 1
fi

for _ in $(seq 1 100); do
  if [[ ! -d "/proc/${first_pid}" ]]; then
    break
  fi
  sleep 0.05
done
if [[ -d "/proc/${first_pid}" ]]; then
  echo "FAIL: old daemon PID ${first_pid} is still present in /proc" >&2
  exit 1
fi

if ! grep -q 'получен сигнал остановки' "${LOG_FILE}"; then
  echo "FAIL: local log does not contain old daemon shutdown message" >&2
  cat "${LOG_FILE}" >&2
  exit 1
fi

kill -TERM "${second_pid}"
for _ in $(seq 1 100); do
  if [[ ! -e "${PID_FILE}" ]]; then
    break
  fi
  sleep 0.05
done
if [[ -e "${PID_FILE}" ]]; then
  echo "FAIL: PID file was not removed after SIGTERM" >&2
  cat "${LOG_FILE}" >&2
  exit 1
fi

echo "PASS: repeated daemon launch stops the old instance and replaces its PID"
