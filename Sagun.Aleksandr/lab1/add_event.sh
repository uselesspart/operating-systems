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

TEXT="$*"

# Текст должен содержать хотя бы один непробельный символ.
if [[ ! "$TEXT" =~ [^[:space:]] ]]; then
    echo "Reminder text is empty" >&2
    exit 1
fi

# Одно событие должно занимать одну строку конфигурации.
if [[ "$TEXT" == *$'\n'* ]] || [[ "$TEXT" == *$'\r'* ]]; then
    echo "Reminder text must not contain line breaks" >&2
    exit 1
fi

if [[ ! "$DATE" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] ||
   [[ ! "$TIME" =~ ^[0-9]{2}:[0-9]{2}$ ]]; then

    echo "Expected date/time format: YYYY-MM-DD HH:MM" >&2
    exit 1
fi

# GNU date проверяет существование даты и допустимость времени.
if ! CHECKED_DATE_TIME="$(
    date -d "$DATE $TIME:00" '+%Y-%m-%d %H:%M' 2>/dev/null
)"; then
    echo "Invalid event date/time: $DATE $TIME" >&2
    exit 1
fi

if [ "$CHECKED_DATE_TIME" != "$DATE $TIME" ]; then
    echo "Invalid event date/time: $DATE $TIME" >&2
    exit 1
fi

# Запись выполняется только после успешной проверки всех полей.
if [ -n "$FLAG" ]; then
    printf '\nadd_event %s %s %s %s\n' \
        "$DATE" "$TIME" "$FLAG" "$TEXT" >> "$CONFIG_FILE"
else
    printf '\nadd_event %s %s %s\n' \
        "$DATE" "$TIME" "$TEXT" >> "$CONFIG_FILE"
fi

echo "Event added"

if [ -f "$PID_FILE" ]; then
    PID="$(cat "$PID_FILE")"

    if [[ "$PID" =~ ^[1-9][0-9]*$ ]] &&
       kill -0 "$PID" 2>/dev/null; then

        if kill -HUP "$PID" 2>/dev/null; then
            echo "Configuration reload requested"
        else
            echo "Event added, but cannot send SIGHUP to daemon" >&2
        fi
    fi
fi