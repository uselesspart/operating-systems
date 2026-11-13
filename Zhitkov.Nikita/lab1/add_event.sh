#!/bin/bash
# Добавляет новое событие в файл событий
# Использование: ./add_event.sh <YYYY-MM-DD> <HH:MM:SS> [-h|-d|-w] <text>
set -e

if [ $# -lt 3 ]; then
    echo "Usage: $0 <date> <time> [flag] <text>" >&2
    echo "  flag: -h (hourly), -d (daily), -w (weekly), omit for one-time" >&2
    exit 1
fi

DATE="$1"
TIME="$2"
shift 2

FLAG=""
case "$1" in
    -h|-d|-w)
        FLAG="$1"
        shift
        ;;
esac

TEXT="$*"

CONFIG="${REMINDER_CONFIG:-./reminder.conf}"
if [ ! -f "$CONFIG" ]; then
    echo "Config file not found: $CONFIG" >&2
    exit 1
fi

EVENTS_FILE="$(grep -E '^[[:space:]]*events_file[[:space:]]*=' "$CONFIG" \
    | head -n1 | cut -d'=' -f2- | tr -d '[:space:]')"

if [ -z "$EVENTS_FILE" ]; then
    echo "events_file not set in $CONFIG" >&2
    exit 1
fi

# Относительный путь трактуем относительно каталога с конфигом
case "$EVENTS_FILE" in
    /*) ;;
    *) EVENTS_FILE="$(dirname "$CONFIG")/$EVENTS_FILE" ;;
esac

mkdir -p "$(dirname "$EVENTS_FILE")"

if [ -n "$FLAG" ]; then
    echo "$DATE $TIME $FLAG $TEXT" >> "$EVENTS_FILE"
else
    echo "$DATE $TIME $TEXT" >> "$EVENTS_FILE"
fi

echo "Event added to $EVENTS_FILE:"
echo "  $DATE $TIME $FLAG $TEXT"

# Сообщаем работающему демону, что надо перечитать конфиг
if [ -f /tmp/reminder_daemon.pid ]; then
    PID="$(cat /tmp/reminder_daemon.pid 2>/dev/null || true)"
    if [ -n "$PID" ] && [ -f "/proc/$PID/comm" ]; then
        COMM="$(cat "/proc/$PID/comm" 2>/dev/null || true)"
        if [ "$COMM" = "reminder_daemon" ]; then
            kill -HUP "$PID" 2>/dev/null && echo "Daemon notified (SIGHUP)"
        fi
    fi
fi