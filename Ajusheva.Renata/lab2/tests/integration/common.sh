#!/usr/bin/env bash

set -u

LAB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BIN=${LAB2_BIN:-$LAB_DIR}
KINDS=${LAB2_KINDS:-pipe fifo sock}

PASS_COUNT=0
FAIL_COUNT=0
WS=$(mktemp -d "${TMPDIR:-/tmp}/lab2-it.XXXXXX")
HOST_PID=""
HOST_LOG=""
declare -A CLIENT_PID=()
declare -A CLIENT_OUT=()
declare -A CLIENT_FD=()
declare -A CLIENT_CODE=()

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

assert_that() {
    local what=$1
    shift
    if "$@"; then
        note_pass "$what"
    else
        note_fail "$what"
    fi
}

has_line() { grep -Fq -- "$2" "$1" 2>/dev/null; }
lacks_line() { ! grep -Fq -- "$2" "$1" 2>/dev/null; }
same() { [ "$1" = "$2" ]; }
alive() { kill -0 "$1" 2>/dev/null; }
gone() { ! kill -0 "$1" 2>/dev/null; }

wait_for() {
    local file=$1 text=$2 limit=${3:-6}
    local deadline=$((SECONDS + limit))
    while [ "$SECONDS" -le "$deadline" ]; do
        if grep -Fq -- "$text" "$file" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

expect_line() {
    local what=$1 file=$2 text=$3 limit=${4:-6}
    if wait_for "$file" "$text" "$limit"; then
        note_pass "$what"
    else
        note_fail "$what" "нет строки «$text» в $(basename "$file"):
$(tail -n 15 "$file" 2>/dev/null)"
    fi
}

start_host() {
    local kind=$1
    shift
    HOST_LOG="$WS/host_$kind.log"
    : >"$HOST_LOG"
    env QT_QPA_PLATFORM=offscreen "$@" "$BIN/host_$kind" >>"$HOST_LOG" 2>&1 &
    HOST_PID=$!
    if ! wait_for "$HOST_LOG" "Хост запущен" 10; then
        note_fail "хост $kind запустился" "$(cat "$HOST_LOG")"
        return 1
    fi
}

stop_host() {
    if [ -n "$HOST_PID" ]; then
        kill -TERM "$HOST_PID" 2>/dev/null
        wait "$HOST_PID" 2>/dev/null
        HOST_PID=""
    fi
}

start_client() {
    local kind=$1 name=$2 target=${3:-$HOST_PID}
    local pipe="$WS/in_$name" out="$WS/out_$name"
    rm -f "$pipe"
    mkfifo "$pipe"
    : >"$out"
    "$BIN/client_$kind" "$target" "$name" <"$pipe" >"$out" 2>&1 &
    CLIENT_PID[$name]=$!
    CLIENT_OUT[$name]=$out
    local fd
    exec {fd}>"$pipe"
    CLIENT_FD[$name]=$fd
}

say() {
    local name=$1
    shift
    printf '%s\n' "$*" >&"${CLIENT_FD[$name]}"
}

close_input() {
    local name=$1
    if [ -n "${CLIENT_FD[$name]:-}" ]; then
        exec {CLIENT_FD[$name]}>&-
        CLIENT_FD[$name]=""
    fi
}

finish_client() {
    local name=$1 limit=${2:-8}
    local pid=${CLIENT_PID[$name]} deadline=$((SECONDS + limit))
    while alive "$pid" && [ "$SECONDS" -le "$deadline" ]; do
        sleep 0.1
    done
    if alive "$pid"; then
        kill -KILL "$pid" 2>/dev/null
    fi
    wait "$pid" 2>/dev/null
    CLIENT_CODE[$name]=$?
    close_input "$name"
}

out_of() { printf '%s' "${CLIENT_OUT[$1]}"; }

cleanup_all() {
    local name
    for name in "${!CLIENT_PID[@]}"; do
        kill -KILL "${CLIENT_PID[$name]}" 2>/dev/null
        close_input "$name"
    done
    if [ -n "$HOST_PID" ]; then
        kill -KILL -- "$HOST_PID" 2>/dev/null
    fi
    wait 2>/dev/null
    rm -rf "$WS"
}

reset_clients() {
    local name
    for name in "${!CLIENT_PID[@]}"; do
        kill -KILL "${CLIENT_PID[$name]}" 2>/dev/null
        wait "${CLIENT_PID[$name]}" 2>/dev/null
        close_input "$name"
    done
    CLIENT_PID=()
    CLIENT_OUT=()
    CLIENT_FD=()
    CLIENT_CODE=()
}

trap cleanup_all EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

summary() {
    echo "  итого: пройдено $PASS_COUNT, провалено $FAIL_COUNT"
    [ "$FAIL_COUNT" -eq 0 ]
}
