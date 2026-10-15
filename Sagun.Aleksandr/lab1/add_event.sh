#!/usr/bin/env bash

set -e

CONFIG_FILE="./config.conf"
PID_FILE="/tmp/mydaemon.pid"

if [ "$#" -lt 3 ]; then
    echo "Usage:"
    echo "$0 DATE TIME [FLAG] TEXT"
    exit 1
fi

DATE="$1"
TIME="$2"

shift 2

FLAG=""

if [ "${1:-}" = "-h" ] ||
   [ "${1:-}" = "-d" ] ||
   [ "${1:-}" = "-w" ]; then

    FLAG="$1"
    shift
fi

if [ "$#" -eq 0 ]; then
    echo "Reminder text is empty"
    exit 1
fi

TEXT="$*"

if [ -n "$FLAG" ]; then
    printf '\nadd_event %s %s %s %s' \
        "$DATE" "$TIME" "$FLAG" "$TEXT" >> "$CONFIG_FILE"
else
    printf '\nadd_event %s %s %s' \
        "$DATE" "$TIME" "$TEXT" >> "$CONFIG_FILE"
fi

echo "Event added"

if [ -f "$PID_FILE" ]; then
    PID="$(cat "$PID_FILE")"

    if kill -0 "$PID" 2>/dev/null; then
        kill -HUP "$PID"
        echo "Daemon configuration reloaded"
    fi
fi