#!/usr/bin/env bash
# Общие функции интеграционных тестов disk_monitor
#
# Каждый тест работает в своём временном каталоге /tmp/dm_test.XXXXXX

set -u

LAB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$LAB_DIR/disk_monitor"
TIMEOUT="${DM_TEST_TIMEOUT:-5}"
QUIET_PERIOD="${DM_TEST_QUIET:-1}"

RULE_FILE=/etc/rsyslog.d/30-disk_monitor.conf
EVENTS_FILE=/var/log/disk_monitor.log
SERVICE_FILE=""

if [ "$(id -u)" -eq 0 ]; then
    PID_FILE=/var/run/disk_monitor.pid
else
    PID_FILE=/tmp/disk_monitor.pid
fi

PASSED=0
FAILED=0
TEST_ROOT=""
DAEMON_PID=""
STARTED_PIDS=()
HELPER_PIDS=()

die()
{
    echo "Ошибка: $*" >&2
    exit 2
}

ok()
{
    PASSED=$((PASSED + 1))
    printf '  ok    %s\n' "$1"
}

fail()
{
    FAILED=$((FAILED + 1))
    printf '  FAIL  %s\n' "$1"
}

check()
{
    local description=$1
    shift
    if "$@"; then
        ok "$description"
    else
        fail "$description"
    fi
}

not() { ! "$@"; }
quietly() { "$@" >/dev/null 2>&1; }
equals() { [ "$1" = "$2" ]; }
file_has() { grep -Eq -- "$2" "$1"; }
dir_is_empty() { [ -z "$(ls -A "$1")" ]; }

file_size()
{
    if [ -r "$1" ]; then
        wc -c <"$1" | tr -d ' '
    else
        echo 0
    fi
}

mark_logs()
{
    EVENTS_OFFSET=$(file_size "$EVENTS_FILE")
    SERVICE_OFFSET=$(file_size "$SERVICE_FILE")
}

read_since() { [ -r "$1" ] && tail -c +"$(($2 + 1))" "$1"; }
events() { read_since "$EVENTS_FILE" "$EVENTS_OFFSET"; }
service() { read_since "$SERVICE_FILE" "$SERVICE_OFFSET"; }

re() { printf '%s' "$1" | sed 's/[][\.*^$+?(){}|]/\\&/g'; }

ev() { echo "disk_monitor\\[[0-9]+\\]: $1 +$2 +$(re "$3")( \\(cookie [0-9]+\\))?\$"; }

wait_for()
{
    local source=$1 pattern=$2 timeout=${3:-$TIMEOUT}
    local deadline=$(($(date +%s) + timeout))
    while :; do
        "$source" | grep -Eq -- "$pattern" && return 0
        [ "$(date +%s)" -ge "$deadline" ] && return 1
        sleep 0.1
    done
}

count_lines() { "$1" | grep -Ec -- "$2"; }

show_tail()
{
    "$1" | tail -n 5 | sed 's/^/        | /'
}

expect_event()
{
    if wait_for events "$2" "${3:-$TIMEOUT}"; then
        ok "$1"
    else
        fail "$1"
        show_tail events
    fi
}

expect_service()
{
    if wait_for service "$2" "${3:-$TIMEOUT}"; then
        ok "$1"
    else
        fail "$1"
        show_tail service
    fi
}

expect_no_event()
{
    sleep "$QUIET_PERIOD"
    if events | grep -Eq -- "$2"; then
        fail "$1"
        events | grep -E -- "$2" | tail -n 3 | sed 's/^/        | /'
    else
        ok "$1"
    fi
}

expect_no_service()
{
    sleep "$QUIET_PERIOD"
    if service | grep -Eq -- "$2"; then
        fail "$1"
    else
        ok "$1"
    fi
}

is_running()
{
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    stat=${stat##*) }
    [ "${stat%% *}" != Z ]
}

process_state()
{
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    stat=${stat##*) }
    echo "${stat%% *}"
}

is_disk_monitor() { is_running "$1" && [ "$(cat "/proc/$1/comm" 2>/dev/null)" = disk_monitor ]; }

wait_exit()
{
    local deadline=$(($(date +%s) + TIMEOUT))
    while is_running "$1"; do
        [ "$(date +%s)" -ge "$deadline" ] && return 1
        sleep 0.1
    done
}

pid_file_value() { cat "$PID_FILE" 2>/dev/null; }

launch()
{
    (cd "$RUN_DIR" && "$BIN" "$@") >"$TEST_ROOT/start.out" 2>&1
    # shellcheck disable=SC2034
    START_STATUS=$?
}

start_daemon()
{
    local before pid
    before=$(pid_file_value)
    DAEMON_PID=""
    START_ERROR=""
    launch "$@"

    local deadline=$(($(date +%s) + TIMEOUT))
    while [ "$(date +%s)" -lt "$deadline" ]; do
        pid=$(pid_file_value)
        if [ -n "$pid" ] && [ "$pid" != "$before" ] && is_disk_monitor "$pid"; then
            DAEMON_PID=$pid
            STARTED_PIDS+=("$pid")
            wait_for events "disk_monitor\\[$pid\\]: === monitoring started" && return 0
            START_ERROR="в $EVENTS_FILE нет записи о запуске наблюдения"
            return 1
        fi
        sleep 0.1
    done
    START_ERROR="pid не появился в $PID_FILE, вывод: $(head -n 3 "$TEST_ROOT/start.out" | tr '\n' ' ')"
    return 1
}

require_daemon()
{
    start_daemon "$@" && return 0
    fail "демон не запустился: $START_ERROR"
    return 1
}

stop_daemon()
{
    local pid=${1:-$DAEMON_PID}
    kill -TERM "$pid" 2>/dev/null
    wait_exit "$pid"
}


setup_test()
{
    TEST_ROOT=$(cd "$(mktemp -d /tmp/dm_test.XXXXXX)" && pwd -P)
    chmod 755 "$TEST_ROOT"
    D1="$TEST_ROOT/dir1"
    D2="$TEST_ROOT/dir2"
    OUT="$TEST_ROOT/outside"
    CONF="$TEST_ROOT/disk_monitor.conf"
    mkdir -p "$D1" "$D2" "$OUT"
    printf '%s\n%s\n' "$D1" "$D2" >"$CONF"
    # shellcheck disable=SC2034
    RUN_DIR=$TEST_ROOT
    DAEMON_PID=""
    STARTED_PIDS=()
    HELPER_PIDS=()
    mark_logs
}

teardown_test()
{
    local pid
    for pid in ${STARTED_PIDS[@]+"${STARTED_PIDS[@]}"}; do
        if is_disk_monitor "$pid"; then
            kill -TERM "$pid" 2>/dev/null
            wait_exit "$pid" || kill -KILL "$pid" 2>/dev/null
        fi
    done
    for pid in ${HELPER_PIDS[@]+"${HELPER_PIDS[@]}"}; do
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    pid=$(pid_file_value)
    if [ -n "$pid" ] && ! is_disk_monitor "$pid"; then
        rm -f "$PID_FILE"
    fi
    [ -n "$TEST_ROOT" ] && rm -rf "$TEST_ROOT"
    TEST_ROOT=""
    STARTED_PIDS=()
    HELPER_PIDS=()
}

run_test()
{
    printf '\n%s\n' "$2"
    setup_test
    "$1"
    teardown_test
}

init_suite()
{
    printf '== %s (%s)\n' "$1" "$(basename "$0")"
    trap 'teardown_test; exit 130' INT TERM

    [ "${2:-}" = build ] && return 0

    [ -x "$BIN" ] || (cd "$LAB_DIR" && sh build.sh >/dev/null) || die "не удалось собрать disk_monitor"

    [ -f "$RULE_FILE" ] || die "нет правила $RULE_FILE, выполните:
  sudo cp rsyslog/30-disk_monitor.conf /etc/rsyslog.d/ && sudo systemctl restart rsyslog"
    pgrep -x rsyslogd >/dev/null || die "rsyslog не запущен"

    local file
    for file in /var/log/syslog /var/log/messages; do
        if [ -f "$file" ]; then
            SERVICE_FILE=$file
            break
        fi
    done
    [ -n "$SERVICE_FILE" ] || die "не найден системный журнал /var/log/syslog или /var/log/messages"
    for file in "$EVENTS_FILE" "$SERVICE_FILE"; do
        [ ! -e "$file" ] || [ -r "$file" ] || die "нет прав на чтение $file, запустите тесты через sudo"
    done

    local pid
    pid=$(pid_file_value)
    if [ -n "$pid" ] && is_disk_monitor "$pid"; then
        die "уже запущен disk_monitor (pid $pid), остановите его: kill -TERM $pid"
    fi
}

finish()
{
    printf '\nИтог: %d ok, %d FAIL\n' "$PASSED" "$FAILED"
    [ "$FAILED" -eq 0 ]
}
