#!/usr/bin/env bash

LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$LIB_DIR/../.." && pwd)"
BIN="$ROOT/daemon_lab1"

if [ "$(id -u)" -eq 0 ]; then
    echo "Тесты нельзя запускать от root" >&2
    exit 2
fi
if [ ! -x "$BIN" ]; then
    echo "Не найден $BIN: сначала выполните ./build.sh" >&2
    exit 2
fi

BIN_REAL="$(readlink -f "$BIN")"
TEST_NAME="$(basename "$0" .sh)"
SUITE_START="$(date '+%Y-%m-%d %H:%M:%S')"
TEST_TMP="$(mktemp -d /tmp/lab1_it.XXXXXX)" || exit 2
TEST_TMP="$(cd "$TEST_TMP" && pwd -P)"
PASSED=0
FAILED=0
DAEMON_PIDS=()
HELPER_PIDS=()
JOURNAL_OK=0
START_OUT=""
START_RC=0
DPID=""
SLEEPER_PID=""

ok() {
    PASSED=$((PASSED + 1))
    echo "  PASS $*"
}

bad() {
    FAILED=$((FAILED + 1))
    echo "  FAIL $*" >&2
}

expect() {
    local desc="$1"
    shift
    if "$@"; then ok "$desc"; else bad "$desc"; fi
}

expect_not() {
    local desc="$1"
    shift
    if "$@"; then bad "$desc"; else ok "$desc"; fi
}

expect_eq() {
    if [ "$2" = "$3" ]; then
        ok "$1"
    else
        bad "$1 (ожидалось '$3', получено '$2')"
    fi
}

contains() {
    case "$1" in
        *"$2"*) return 0 ;;
    esac
    return 1
}

wait_until() {
    local timeout="$1" i
    shift
    for ((i = 0; i < timeout * 10; i++)); do
        if "$@"; then return 0; fi
        sleep 0.1
    done
    "$@"
}

is_ours() {
    local exe
    exe="$(readlink "/proc/$1/exe" 2>/dev/null)" || return 1
    [ "${exe% (deleted)}" = "$BIN_REAL" ]
}

not_alive() {
    ! is_ours "$1"
}

is_sleeper() {
    [ "$(basename "$(readlink "/proc/$1/exe" 2>/dev/null)")" = "sleep" ]
}

any_daemon_alive() {
    local pid
    for pid in "${DAEMON_PIDS[@]}"; do
        if is_ours "$pid"; then return 0; fi
    done
    return 1
}

signal_daemon() {
    if is_ours "$2"; then
        kill "-$1" "$2"
    else
        bad "сигнал $1: процесс '$2' не является нашим демоном"
        return 1
    fi
}

spawn_sleeper() {
    sleep 300 >/dev/null 2>&1 &
    SLEEPER_PID=$!
    disown "$SLEEPER_PID"
    HELPER_PIDS+=("$SLEEPER_PID")
}

stop_all() {
    local pid i
    for pid in "${DAEMON_PIDS[@]}"; do
        if is_ours "$pid"; then kill -TERM "$pid" 2>/dev/null; fi
    done
    for i in $(seq 1 50); do
        any_daemon_alive || break
        sleep 0.1
    done
    for pid in "${DAEMON_PIDS[@]}"; do
        if is_ours "$pid"; then kill -KILL "$pid" 2>/dev/null; fi
    done
    for pid in "${HELPER_PIDS[@]}"; do
        if is_sleeper "$pid"; then kill -KILL "$pid" 2>/dev/null; fi
    done
    HELPER_PIDS=()
}

remove_tmp() {
    case "$TEST_TMP" in
        /tmp/lab1_it.??????) rm -rf -- "$TEST_TMP" ;;
    esac
}

finish() {
    stop_all
    expect_not "после теста не осталось процессов демона" any_daemon_alive
    remove_tmp
    trap - EXIT
    echo "$TEST_NAME: $PASSED passed, $FAILED failed"
    if [ "$FAILED" -eq 0 ]; then exit 0; fi
    exit 1
}

start_daemon() {
    START_OUT="$( (cd "${START_DIR:-$TEST_TMP}" && "$BIN" "$@" 2>&1) )"
    START_RC=$?
    DPID=""
    local pid
    pid="$(printf '%s\n' "$START_OUT" | sed -n 's/.*started, PID \([0-9][0-9]*\).*/\1/p' | tail -n 1)"
    if [ -n "$pid" ]; then
        DPID="$pid"
        DAEMON_PIDS+=("$pid")
    fi
}

require_started() {
    if [ "$START_RC" -ne 0 ] || [ -z "$DPID" ]; then
        bad "демон не запустился (код $START_RC): $START_OUT"
        finish
    fi
}

make_conf() {
    local file="$1" interval="$2" rule
    shift 2
    {
        echo "interval $interval"
        for rule in "$@"; do
            echo "$rule"
        done
    } > "$file"
}

fill_dir() {
    local d="$1"
    mkdir -p "$d/sub/deep" "$d/.hidden_dir"
    echo a > "$d/a.txt"
    echo h > "$d/.hidden"
    echo b > "$d/sub/b.txt"
    echo c > "$d/sub/deep/c.txt"
    echo d > "$d/.hidden_dir/d"
}

count_all() {
    find "$1" -mindepth 1 2>/dev/null | wc -l
}

dir_empty() {
    [ -d "$1" ] && [ "$(count_all "$1")" -eq 0 ]
}

pid_of() {
    { tr -d '[:space:]' < "$1"; } 2>/dev/null
}

proc_sid() {
    ps -o sid= -p "$1" 2>/dev/null | tr -d ' '
}

proc_tty() {
    ps -o tty= -p "$1" 2>/dev/null | tr -d ' '
}

proc_ppid() {
    ps -o ppid= -p "$1" 2>/dev/null | tr -d ' '
}

journal_probe() {
    journalctl -t "lab1it$$" --since "$SUITE_START" -o cat --no-pager 2>/dev/null |
        grep -qF "probe $TEST_TMP"
}

init_journal() {
    JOURNAL_OK=0
    command -v journalctl >/dev/null 2>&1 || return 0
    command -v logger >/dev/null 2>&1 || return 0
    logger -t "lab1it$$" "probe $TEST_TMP"
    if wait_until 8 journal_probe; then JOURNAL_OK=1; fi
    if [ "$JOURNAL_OK" -eq 0 ]; then
        echo "  SKIP журнал недоступен, проверки journalctl будут пропущены"
    fi
}

journal_has() {
    journalctl -t daemon_lab1 --since "$SUITE_START" -o cat --no-pager 2>/dev/null |
        grep -qF -- "$1"
}

journal_has_pid() {
    journalctl "_PID=$1" --since "$SUITE_START" -o cat --no-pager 2>/dev/null |
        grep -qF -- "$2"
}

expect_journal() {
    if [ "$JOURNAL_OK" -ne 1 ]; then
        echo "  SKIP $1"
        return 0
    fi
    if wait_until 8 journal_has "$2"; then
        ok "$1"
    else
        bad "$1 (в журнале нет '$2')"
    fi
}

expect_journal_pid() {
    if [ "$JOURNAL_OK" -ne 1 ]; then
        echo "  SKIP $1"
        return 0
    fi
    if wait_until 8 journal_has_pid "$2" "$3"; then
        ok "$1"
    else
        bad "$1 (в журнале процесса $2 нет '$3')"
    fi
}
