#!/usr/bin/env bash
# Общие функции интеграционных тестов; подключаются из tests/integration/NN_*.sh.


set -u

export LC_ALL=C

LAB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
DAEMON="$LAB_DIR/diskmond"
PATIENCE=${DISKMOND_TEST_PATIENCE:-5}  
SILENCE=${DISKMOND_TEST_SILENCE:-0.6}  

PASS_COUNT=0
FAIL_COUNT=0
SKIP_COUNT=0
WS=""
LAUNCHED=()
HELPERS=()



note_pass() {
    PASS_COUNT=$((PASS_COUNT + 1))
    printf '  [+] %s\n' "$1"
}

note_fail() {
    FAIL_COUNT=$((FAIL_COUNT + 1))
    printf '  [x] %s\n' "$1"
    if [ -n "${2:-}" ]; then
        printf '%s\n' "$2" | sed 's/^/        > /'
    fi
}

note_skip() {
    SKIP_COUNT=$((SKIP_COUNT + 1))
    printf '  [-] %s\n' "$1"
}


assert_that() {
    local what=$1
    shift
    if "$@"; then
        note_pass "$what"
    else
        note_fail "$what"
    fi
}

same() { [ "$1" = "$2" ]; }
differ() { [ "$1" != "$2" ]; }
negated() { ! "$@"; }
muted() { "$@" >/dev/null 2>&1; }
has_line() { grep -Eq -- "$2" "$1" 2>/dev/null; }
empty_dir() { [ -z "$(ls -A "$1")" ]; }


literal() { printf '%s' "$1" | sed 's/[][\.*^$+?(){}|]/\\&/g'; }



captured() { cat "$CAPTURE" 2>/dev/null; }
remember_position() { POSITION=$(captured | wc -l); }
since_position() { tail -n +"$((POSITION + 1))" "$CAPTURE" 2>/dev/null; }


local0_lines() { since_position | grep -E '^local0\.' || true; }
daemon_lines() { since_position | grep -E '^daemon\.' || true; }


entry_re() {
    local pattern
    pattern="^local0\\.info diskmond\\[[0-9]+\\]: $1 +$(literal "$2")"
    if [ $# -ge 3 ]; then
        pattern+=" -> $(literal "$3")"
    fi
    printf '%s$' "$pattern"
}

tally() { "$1" | grep -Ec -- "$2" || true; }


await() {
    local source=$1 pattern=$2 deadline=$((SECONDS + ${3:-$PATIENCE}))
    until "$source" | grep -Eq -- "$pattern"; do
        if [ "$SECONDS" -ge "$deadline" ]; then
            return 1
        fi
        sleep 0.05
    done
}


expect_line() {
    if await "$2" "$3"; then
        note_pass "$1"
    else
        note_fail "$1" "нет строки: $3
в конце было:
$("$2" | tail -n 6)"
    fi
}


expect_silence() {
    sleep "$SILENCE"
    if "$2" | grep -Eq -- "$3"; then
        note_fail "$1" "$("$2" | grep -E -- "$3" | head -n 3)"
    else
        note_pass "$1"
    fi
}


stat_fields() {
    local line
    line=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    printf '%s\n' "${line##*) }"
}

alive() {
    local fields
    fields=$(stat_fields "$1") || return 1
    [ "${fields%% *}" != Z ]
}

is_our_daemon() { alive "$1" && [ "$(cat "/proc/$1/comm" 2>/dev/null)" = diskmond ]; }

await_exit() {
    local deadline=$((SECONDS + PATIENCE))
    while alive "$1"; do
        if [ "$SECONDS" -ge "$deadline" ]; then
            return 1
        fi
        sleep 0.05
    done
}


launch() {
    (cd "$START_DIR" && DISKMOND_SYSLOG_CAPTURE="$CAPTURE" LD_PRELOAD="$CAPTURE_LIB" "$DAEMON" -p "$PIDF" "$@") \
        >"$WS/stdout.txt" 2>"$WS/stderr.txt"
    LAUNCH_CODE=$?
    PID=""
    if [ "$LAUNCH_CODE" -eq 0 ]; then
        PID=$(cat "$PIDF" 2>/dev/null)
    fi
    if [ -n "$PID" ]; then
        LAUNCHED+=("$PID")
        return 0
    fi
    return 1
}


launch_ok() {
    if launch "$@"; then
        return 0
    fi
    note_fail "демон не запустился (код $LAUNCH_CODE)" "$(cat "$WS/stderr.txt" "$WS/stdout.txt")"
    return 1
}

terminate() {
    kill -TERM "${1:-$PID}" 2>/dev/null
    await_exit "${1:-$PID}"
}



fresh_workspace() {
    WS=$(cd "$(mktemp -d "${TMPDIR:-/tmp}/diskmond-it.XXXXXX")" && pwd -P)
    chmod 755 "$WS"
    DIR_A="$WS/a"
    DIR_B="$WS/b"
    OUTSIDE="$WS/elsewhere"
    mkdir -p "$DIR_A" "$DIR_B" "$OUTSIDE"
    CONF="$WS/diskmond.conf"
    printf 'watch %s\nwatch %s\n' "$DIR_A" "$DIR_B" >"$CONF"
    CAPTURE="$WS/captured.log"
    : >"$CAPTURE"
    PIDF="$WS/diskmond.pid"
    START_DIR=$WS
    POSITION=0
    PID=""
    LAUNCHED=()
    HELPERS=()
}

clean_workspace() {
    local pid
    for pid in ${LAUNCHED[@]+"${LAUNCHED[@]}"}; do
        if is_our_daemon "$pid"; then
            kill -TERM "$pid" 2>/dev/null
            await_exit "$pid" || kill -KILL "$pid" 2>/dev/null
        fi
    done
    for pid in ${HELPERS[@]+"${HELPERS[@]}"}; do
        kill "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
    done
    LAUNCHED=()
    HELPERS=()
    if [ -n "$WS" ]; then
        rm -rf "$WS"
    fi
    WS=""
}


scenario() {
    printf '\n* %s\n' "$2"
    fresh_workspace
    "$1"
    clean_workspace
}


provide_capture_library() {
    CAPTURE_LIB=${DISKMOND_CAPTURE_LIB:-}
    if [ -n "$CAPTURE_LIB" ] && [ -f "$CAPTURE_LIB" ]; then
        return 0
    fi
    PRIVATE_TMP=$(mktemp -d "${TMPDIR:-/tmp}/diskmond-capture.XXXXXX")
    CAPTURE_LIB="$PRIVATE_TMP/libsyslog_capture.so"
    ${CXX:-g++} -std=c++17 -O2 -Wall -Wextra -Werror -fPIC -shared \
        "$LAB_DIR/tests/preload/syslog_capture.cpp" -o "$CAPTURE_LIB" ||
        { echo "не удалось собрать tests/preload/syslog_capture.cpp" >&2; exit 2; }
}

leave_suite() {
    clean_workspace
    if [ -n "${PRIVATE_TMP:-}" ]; then
        rm -rf "$PRIVATE_TMP"
    fi
}

begin_suite() {
    printf '### %s — %s\n' "$(basename "$0")" "$1"
    trap leave_suite EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
    if [ "${2:-}" = no-daemon ]; then
        return 0
    fi
    if [ ! -x "$DAEMON" ]; then
        (cd "$LAB_DIR" && sh build.sh >/dev/null) || { echo "не удалось собрать diskmond" >&2; exit 2; }
    fi
    provide_capture_library
}

end_suite() {
    printf '\n  итого: пройдено %d, провалено %d' "$PASS_COUNT" "$FAIL_COUNT"
    if [ "$SKIP_COUNT" -gt 0 ]; then
        printf ', пропущено %d' "$SKIP_COUNT"
    fi
    printf '\n'
    [ "$FAIL_COUNT" -eq 0 ]
}
