#!/bin/bash

set -e

echo "==> Preparing the test environment..."
TEST_DIR="/tmp/test_monitor_dir"
CONFIG_FILE="/tmp/test_daemon.conf"
LOG_FILE="/var/log/disk_monitor.log"
PID_FILE="/tmp/disk_monitor.pid"

mkdir -p "$TEST_DIR"
echo "DIR=$TEST_DIR" > "$CONFIG_FILE"

> "$LOG_FILE"

echo "==> Starting the daemon..."
./out/disk_monitor "$CONFIG_FILE"

sleep 1

if [ ! -f "$PID_FILE" ]; then
    echo "ERROR: PID file not created. Daemon crashed during startup."
    exit 1
fi

DAEMON_PID=$(cat "$PID_FILE")
echo "Daemon started successfully (PID: $DAEMON_PID)"

echo "==> Triggering filesystem events (inotify)..."
touch "$TEST_DIR/hello.txt"
echo "test data" > "$TEST_DIR/hello.txt"
rm "$TEST_DIR/hello.txt"

sleep 1

echo "==> Checking syslog..."
cat "$LOG_FILE"

if grep -q "CREATED.*hello.txt" "$LOG_FILE" && \
   grep -q "DELETED.*hello.txt" "$LOG_FILE"; then
    echo "SUCCESS: inotify events correctly caught and logged!"
else
    echo "ERROR: Events not found in log."
    kill -TERM "$DAEMON_PID"
    exit 1
fi

echo "==> Testing shutdown (SIGTERM)..."
kill -TERM "$DAEMON_PID"
sleep 1

if [ -f "$PID_FILE" ]; then
    echo "ERROR: PID file not deleted. Daemon did not shut down correctly."
    exit 1
fi

echo "SUCCESS: Daemon shut down correctly and cleaned up after itself."
echo "Integration test PASSED!"