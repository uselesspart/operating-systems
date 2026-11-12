#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
PID_FILE="$TMP/daemon.pid"
DAEMON_PID=""

cleanup() {
    if [[ -f "$PID_FILE" ]]; then
        local pid
        pid="$(cat "$PID_FILE" 2>/dev/null || true)"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
            sleep 1
        fi
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

fail() {
    echo "[FAIL] $1" >&2
    exit 1
}

pass() {
    echo "[PASS] $1"
}

wait_for_file() {
    local path="$1"
    for _ in {1..50}; do
        [[ -f "$path" ]] && return 0
        sleep 0.1
    done
    return 1
}

wait_until_missing() {
    local path="$1"
    for _ in {1..50}; do
        [[ ! -e "$path" ]] && return 0
        sleep 0.1
    done
    return 1
}

wait_until_dead() {
    local pid="$1"
    for _ in {1..50}; do
        [[ ! -d "/proc/$pid" ]] && return 0
        sleep 0.1
    done
    return 1
}

cd "$ROOT"
./build.sh >/dev/null
[[ -x ./daemon14 ]] || fail "build.sh did not create daemon14"
[[ ! -d build ]] || fail "build.sh left intermediate build directory"
pass "build.sh: -Wall/-Werror build and cleanup"

mkdir -p "$TMP/folder_a/sub" "$TMP/folder_b"
echo "delete me" > "$TMP/folder_a/file.txt"
echo "nested" > "$TMP/folder_a/sub/nested.txt"
echo "keep me" > "$TMP/folder_b/file.txt"
touch "$TMP/folder_b/keep.me"

cat > "$TMP/config.conf" <<CFG
interval 1
folder_a dont.erase
folder_b keep.me
CFG

OS_LAB1_PID_FILE="$PID_FILE" ./daemon14 "$TMP/config.conf"
wait_for_file "$PID_FILE" || fail "pid file was not created"
DAEMON_PID="$(cat "$PID_FILE")"
[[ -d "/proc/$DAEMON_PID" ]] || fail "daemon process is not running"
pass "daemon starts and writes pid file"

wait_until_missing "$TMP/folder_a/file.txt" || fail "folder without marker was not cleaned"
[[ -z "$(find "$TMP/folder_a" -mindepth 1 -print -quit)" ]] || fail "folder_a is not empty"
[[ -f "$TMP/folder_b/file.txt" && -f "$TMP/folder_b/keep.me" ]] || fail "folder with marker was modified"
pass "variant 14: clean without marker, preserve with marker"

echo "periodic" > "$TMP/folder_a/again.txt"
wait_until_missing "$TMP/folder_a/again.txt" || fail "periodic processing did not repeat"
pass "periodic processing uses interval from config"

mkdir -p "$TMP/folder_c"
echo "reload" > "$TMP/folder_c/reload.txt"
echo "folder_c dont.erase" >> "$TMP/config.conf"
kill -HUP "$DAEMON_PID"
wait_until_missing "$TMP/folder_c/reload.txt" || fail "SIGHUP did not reload config"
[[ "$(cat "$PID_FILE")" == "$DAEMON_PID" ]] || fail "SIGHUP restarted daemon instead of reloading"
pass "SIGHUP reloads configuration without restart"

OLD_PID="$DAEMON_PID"
OS_LAB1_PID_FILE="$PID_FILE" ./daemon14 "$TMP/config.conf"
for _ in {1..50}; do
    if [[ -f "$PID_FILE" ]]; then
        NEW_PID="$(cat "$PID_FILE")"
        if [[ "$NEW_PID" != "$OLD_PID" && -d "/proc/$NEW_PID" ]]; then
            DAEMON_PID="$NEW_PID"
            break
        fi
    fi
    sleep 0.1
done
[[ "$DAEMON_PID" != "$OLD_PID" ]] || fail "second launch did not replace old daemon"
wait_until_dead "$OLD_PID" || fail "old daemon is still running"
pass "pid file + /proc protect against repeated launch"

kill -TERM "$DAEMON_PID"
wait_until_dead "$DAEMON_PID" || fail "SIGTERM did not stop daemon"
for _ in {1..20}; do
    [[ ! -e "$PID_FILE" ]] && break
    sleep 0.1
done
[[ ! -e "$PID_FILE" ]] || fail "pid file was not removed after SIGTERM"
pass "SIGTERM stops daemon and removes pid file"

echo
echo "All tests passed."
