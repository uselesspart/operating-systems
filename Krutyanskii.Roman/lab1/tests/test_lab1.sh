#!/usr/bin/env bash
# Integration tests for lab1 daemon (variant 14).
# No extra dependencies beyond bash / coreutils / g++.
# Run from repo root or tests/; runs sequentially and cleans up daemon after each case.

set -uo pipefail

TEST_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TEST_DIR")"
DAEMON="${PROJECT_DIR}/lab1daemon"
PIDFILE="/tmp/lab1daemon.pid"

PASS=0
FAIL=0

fail() { echo "FAIL: $1"; ((FAIL++)) || true; }
pass() { echo "PASS: $1"; ((PASS++)) || true; }

cleanup_daemon() {
    if [[ -f "$PIDFILE" ]]; then
        local pid
        pid="$(cat "$PIDFILE" 2>/dev/null)" || true
        if [[ -n "${pid:-}" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
            sleep 1
            kill -KILL "$pid" 2>/dev/null || true
        fi
        rm -f "$PIDFILE"
    fi
}
trap cleanup_daemon EXIT

run_daemon() {
    local conf="$1"
    # Parent blocks until child reports startup; exit code is startup verdict.
    "$DAEMON" "$conf"
}

# ---------- 1. build ----------
echo "=== test_build ==="
if [ -x "$DAEMON" ]; then
    pass "binary exists"
else
    (cd "$PROJECT_DIR" && bash build.sh >/dev/null 2>&1)
    if [ -x "$DAEMON" ]; then pass "build.sh creates binary"; else fail "build.sh missing binary"; fi
fi

# ---------- 2. valid config ----------
echo "=== test_config_valid ==="
TMP_CONF=$(mktemp)
echo "/tmp/lab1-test-folder dont.erase 2" > "$TMP_CONF"
mkdir -p /tmp/lab1-test-folder
cleanup_daemon
if run_daemon "$TMP_CONF"; then
    pass "daemon starts with valid config"
else
    fail "daemon failed to start with valid config"
fi
if [[ -f "$PIDFILE" ]]; then
    DAEMON_PID=$(cat "$PIDFILE")
    if kill -0 "$DAEMON_PID" 2>/dev/null; then pass "pid file matches running process"; else fail "pid file stale"; fi
else
    fail "pid file missing after start"
fi
cleanup_daemon
rm -f "$TMP_CONF"

# ---------- 3. invalid config ----------
echo "=== test_config_invalid ==="
TMP_CONF=$(mktemp)
echo "badline" > "$TMP_CONF"
cleanup_daemon
if run_daemon "$TMP_CONF"; then
    fail "daemon started with invalid config"
else
    pass "daemon rejects invalid config"
fi
rm -f "$TMP_CONF"

# ---------- 4. sigterm stops ----------
echo "=== test_sigterm ==="
TMP_CONF=$(mktemp)
echo "/tmp/lab1-test-folder dont.erase 2" > "$TMP_CONF"
mkdir -p /tmp/lab1-test-folder
cleanup_daemon
run_daemon "$TMP_CONF" || true
if [[ -f "$PIDFILE" ]]; then
    PID=$(cat "$PIDFILE")
    kill -TERM "$PID" 2>/dev/null || true
    for i in {1..10}; do
        if ! kill -0 "$PID" 2>/dev/null; then break; fi
        sleep 0.5
    done
    if kill -0 "$PID" 2>/dev/null; then
        kill -KILL "$PID" 2>/dev/null || true
        fail "daemon did not exit on SIGTERM"
    else
        pass "daemon exits on SIGTERM"
    fi
else
    fail "pid file missing for sigterm test"
fi
cleanup_daemon
rm -f "$TMP_CONF"

# ---------- 5. pidfile singleton (second instance kills/replaces) ----------
echo "=== test_pidfile_singleton ==="
TMP_CONF=$(mktemp)
echo "/tmp/lab1-test-folder dont.erase 2" > "$TMP_CONF"
mkdir -p /tmp/lab1-test-folder
cleanup_daemon
run_daemon "$TMP_CONF" || true
OLD_PID=""
if [[ -f "$PIDFILE" ]]; then OLD_PID=$(cat "$PIDFILE"); fi
if [[ -z "$OLD_PID" ]]; then fail "first daemon did not start"; else pass "first daemon started (pid $OLD_PID)"; fi
# Second start should see old instance and either wait/restart
sleep 0.2
# Because stopPreviousInstance sends SIGTERM and waits up to 5s, second run may succeed after old dies.
# We just verify it does not leave two processes with the same pid file.
run_daemon "$TMP_CONF" || true
NEW_PID=""
if [[ -f "$PIDFILE" ]]; then NEW_PID=$(cat "$PIDFILE"); fi
if [[ -n "$NEW_PID" ]] && kill -0 "$NEW_PID" 2>/dev/null; then
    pass "pid file points to running daemon after restart"
else
    fail "daemon not running after second start"
fi
if [[ -n "${OLD_PID:-}" ]] && kill -0 "$OLD_PID" 2>/dev/null; then
    fail "old daemon still alive (singleton violated)"
else
    pass "old daemon terminated (singleton ok)"
fi
cleanup_daemon
rm -f "$TMP_CONF"

# ---------- 6. cleanup when ignfile missing (variant 14) ----------
echo "=== test_cleanup_no_ignore ==="
TMP_DIR=$(mktemp -d)
echo "$TMP_DIR/sub dont.erase 2" > "$TMP_DIR/conf"
mkdir -p "$TMP_DIR/sub"
echo "hello" > "$TMP_DIR/sub/file.txt"
cleanup_daemon
run_daemon "$TMP_DIR/conf" || true
if [[ -f "$PIDFILE" ]]; then PID=$(cat "$PIDFILE"); else PID=""; fi
# Wait for interval (2 sec) + margin
sleep 3
if [[ -f "$TMP_DIR/sub/file.txt" ]]; then
    fail "file not removed when ignfile absent"
else
    pass "folder cleaned when ignfile missing"
fi
cleanup_daemon
rm -rf "$TMP_DIR"

# ---------- 7. ignore file protects ----------
echo "=== test_ignore_file_protects ==="
TMP_DIR=$(mktemp -d)
echo "$TMP_DIR/sub dont.erase 2" > "$TMP_DIR/conf"
mkdir -p "$TMP_DIR/sub"
touch "$TMP_DIR/sub/dont.erase"
echo "protect" > "$TMP_DIR/sub/file.txt"
cleanup_daemon
run_daemon "$TMP_DIR/conf" || true
sleep 3
if [[ -f "$TMP_DIR/sub/file.txt" ]]; then
    pass "ignore file protects folder contents"
else
    fail "ignore file did not protect folder"
fi
cleanup_daemon
rm -rf "$TMP_DIR"

# ---------- 8. sighup reload (does not die) ----------
echo "=== test_sighup_reload ==="
TMP_CONF=$(mktemp)
echo "/tmp/lab1-test-folder dont.erase 2" > "$TMP_CONF"
mkdir -p /tmp/lab1-test-folder
cleanup_daemon
run_daemon "$TMP_CONF" || true
if [[ -f "$PIDFILE" ]]; then PID=$(cat "$PIDFILE"); else PID=""; fi
if [[ -n "$PID" ]]; then
    kill -HUP "$PID" 2>/dev/null || true
    sleep 0.5
    if kill -0 "$PID" 2>/dev/null; then pass "daemon survives SIGHUP"; else fail "daemon died on SIGHUP"; fi
else
    fail "no pid for sighup test"
fi
cleanup_daemon
rm -f "$TMP_CONF"

# ---------- 9. relative folder is anchored to the config file ----------
echo "=== test_relative_folder ==="
TMP_DIR=$(mktemp -d)
mkdir -p "$TMP_DIR/sub"
echo "sub dont.erase 2" > "$TMP_DIR/conf"
echo "hello" > "$TMP_DIR/sub/file.txt"
cleanup_daemon
run_daemon "$TMP_DIR/conf" || fail "daemon failed to start with a relative folder"
sleep 3
if [[ -f "$TMP_DIR/sub/file.txt" ]]; then
    fail "relative folder was not resolved next to the config file"
else
    pass "relative folder resolved against the config directory"
fi
cleanup_daemon
rm -rf "$TMP_DIR"

# ---------- 10. the root directory is refused ----------
echo "=== test_root_folder_rejected ==="
TMP_CONF=$(mktemp)
echo "/ dont.erase" > "$TMP_CONF"
cleanup_daemon
if run_daemon "$TMP_CONF"; then
    fail "daemon accepted '/' as a rule folder"
else
    pass "root directory refused as a rule folder"
fi
rm -f "$TMP_CONF"
TMP_DIR=$(mktemp -d)
echo "../../.. dont.erase" > "$TMP_DIR/conf"
if run_daemon "$TMP_DIR/conf"; then
    fail "daemon accepted a '..' folder escaping to the root"
else
    pass "folder escaping to the root refused"
fi
rm -rf "$TMP_DIR"

# ---------- 11. an interval above 20 s fires on time ----------
echo "=== test_interval_above_20 ==="
TMP_DIR=$(mktemp -d)
echo "$TMP_DIR/sub dont.erase 21" > "$TMP_DIR/conf"
mkdir -p "$TMP_DIR/sub"
touch "$TMP_DIR/sub/dont.erase"     # keep the folder safe during the first pass
cleanup_daemon
run_daemon "$TMP_DIR/conf" || fail "daemon failed to start with interval 21"
sleep 1
rm -f "$TMP_DIR/sub/dont.erase"     # the rule is due ~21 s after the start
echo "hello" > "$TMP_DIR/sub/file.txt"
# A 20 s wake-up grid would leave the folder untouched until the 40 s mark,
# so the file must disappear within 25 s of the guard removal.
CLEARED=""
for _ in $(seq 1 50); do
    if [[ ! -f "$TMP_DIR/sub/file.txt" ]]; then CLEARED=1; break; fi
    sleep 0.5
done
if [[ -n "$CLEARED" ]]; then
    pass "interval above 20 s honored (folder cleared in under 25 s)"
else
    fail "interval of 21 s was not honored within 25 s"
fi
cleanup_daemon
rm -rf "$TMP_DIR"

# ---------- 12. restart after the binary has been rebuilt ----------
echo "=== test_restart_after_rebuild ==="
TMP_DIR=$(mktemp -d)
COPY="$TMP_DIR/lab1daemon"
cp "$DAEMON" "$COPY"
echo "/tmp/lab1-test-folder dont.erase 2" > "$TMP_DIR/conf"
mkdir -p /tmp/lab1-test-folder
cleanup_daemon
"$COPY" "$TMP_DIR/conf" || fail "copied daemon failed to start"
OLD_PID=""
if [[ -f "$PIDFILE" ]]; then OLD_PID=$(cat "$PIDFILE"); fi
if [[ -z "$OLD_PID" ]]; then
    fail "no pid file from the first (copied) daemon"
else
    # Replace the binary: the new inode makes /proc/<pid>/exe of the running
    # daemon read as "... (deleted)", exactly as after a rebuild.
    rm -f "$COPY" && cp "$DAEMON" "$COPY"
    "$COPY" "$TMP_DIR/conf" || fail "restart after rebuild was refused"
    NEW_PID=""
    if [[ -f "$PIDFILE" ]]; then NEW_PID=$(cat "$PIDFILE"); fi
    if kill -0 "$OLD_PID" 2>/dev/null; then
        fail "old daemon survived a restart after the binary was rebuilt"
    else
        pass "old daemon stopped after the binary was rebuilt"
    fi
    if [[ -n "$NEW_PID" ]] && kill -0 "$NEW_PID" 2>/dev/null; then
        pass "new daemon running after the rebuild"
    else
        fail "new daemon not running after the rebuild"
    fi
fi
cleanup_daemon
rm -rf "$TMP_DIR"

echo ""
echo "Results: $PASS passed, $FAIL failed"
exit $FAIL
