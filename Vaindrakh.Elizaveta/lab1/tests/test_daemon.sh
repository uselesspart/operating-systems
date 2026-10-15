#!/usr/bin/env bash

set -eu

cd "$(dirname "$0")/.."

fail() {
    echo "FAIL: $1" >&2
    if [ -n "${daemon_pid:-}" ] && kill -0 "$daemon_pid" 2>/dev/null; then
        kill -TERM "$daemon_pid" 2>/dev/null || true
    fi
    exit 1
}

wait_for() {
    local condition=$1
    local attempts=0
    while [ "$attempts" -lt 100 ]; do
        if eval "$condition"; then
            return 0
        fi
        attempts=$((attempts + 1))
        sleep 0.05
    done
    return 1
}

temporary_directory=$(mktemp -d "${TMPDIR:-/tmp}/disk-monitor-test.XXXXXX")
trap 'rm -rf "$temporary_directory"' EXIT

first_directory="$temporary_directory/first"
second_directory="$temporary_directory/second"
mkdir -p "$first_directory" "$second_directory"
config_file="$temporary_directory/monitor.conf"
test_log="$temporary_directory/test.log"
pid_file="$temporary_directory/monitor.pid"
printf 'DIR=%s\n' "$first_directory" > "$config_file"

DISK_MONITOR_PIDFILE="$pid_file" DISK_MONITOR_TEST_LOG="$test_log" ./disk_monitor "$config_file"
wait_for "test -s '$pid_file'" || fail "pid file was not created"
daemon_pid=$(cat "$pid_file")
wait_for "kill -0 '$daemon_pid' 2>/dev/null" || fail "daemon is not running"
wait_for "test -e '/proc/$daemon_pid/cwd'" || fail "daemon process disappeared"
wait_for "grep -q 'watching 1 configured directory' '$test_log'" || fail "daemon did not load configuration"

[ "$(readlink "/proc/$daemon_pid/cwd")" = "/" ] || fail "daemon did not change cwd to /"
[ "$(readlink "/proc/$daemon_pid/fd/0")" = "/dev/null" ] || fail "stdin is not /dev/null"
[ "$(readlink "/proc/$daemon_pid/fd/1")" = "/dev/null" ] || fail "stdout is not /dev/null"
[ "$(readlink "/proc/$daemon_pid/fd/2")" = "/dev/null" ] || fail "stderr is not /dev/null"

printf 'hello\n' > "$first_directory/file.txt"
wait_for "grep -q 'path=$first_directory/file.txt' '$test_log'" || fail "file event was not logged"
grep -q 'local0 .*event=' "$test_log" || fail "event was not sent with local0 facility"
mkdir "$first_directory/nested"
printf 'nested\n' > "$first_directory/nested/nested.txt"
wait_for "grep -q 'path=$first_directory/nested/nested.txt' '$test_log'" || fail "recursive event was not logged"

printf 'DIR=%s\n' "$first_directory" "$second_directory" > "$config_file"
kill -HUP "$daemon_pid"
wait_for "grep -q 'configuration reloaded' '$test_log'" || fail "configuration was not reloaded"
printf 'after reload\n' > "$second_directory/reloaded.txt"
wait_for "grep -q 'path=$second_directory/reloaded.txt' '$test_log'" || fail "new directory was not watched after SIGHUP"

kill -TERM "$daemon_pid"
wait_for "! kill -0 '$daemon_pid' 2>/dev/null" || fail "daemon did not stop after SIGTERM"
wait_for "! test -e '$pid_file'" || fail "pid file was not removed"
grep -q 'daemon stopping after termination signal' "$test_log" || fail "shutdown was not logged"

echo "Daemon integration test passed"

