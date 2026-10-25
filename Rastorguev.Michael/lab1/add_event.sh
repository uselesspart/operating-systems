#!/bin/bash

CONF="${SCHEDULE_CONF:-./schedule.conf}"
PID_FILE=/tmp/schedule_daemon.pid

date_s="$1"; time_s="$2"; shift 2

flag=""
case "$1" in -h|-d|-w) flag="$1"; shift ;; esac

text="$*"

echo "add_event $date_s $time_s ${flag:+$flag }$text" >> "$CONF" || exit 1
echo "Событие добавлено в $CONF"

if [ -r "$PID_FILE" ]; then
    pid=$(cat "$PID_FILE")
    if [[ "$pid" =~ ^[0-9]+$ ]] && [ -d "/proc/$pid" ]; then
        kill -HUP "$pid" && echo "Демону (PID $pid) отправлен SIGHUP"
    fi
fi