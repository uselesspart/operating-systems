#!/bin/sh
set -u

if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
    PIDFILE="$XDG_RUNTIME_DIR/reminder_daemon.pid"
else
    PIDFILE="/tmp/reminder_daemon.pid"
fi

if [ ! -r "$PIDFILE" ]; then
    echo "Демон не запущен: нет файла $PIDFILE"
    exit 0
fi

PID=$(cat "$PIDFILE" 2>/dev/null || true)
case "$PID" in
    ''|*[!0-9]*)
        echo "В файле $PIDFILE нет корректного PID" >&2
        exit 1
        ;;
esac

if [ ! -r "/proc/$PID/comm" ] || [ "$(cat "/proc/$PID/comm" 2>/dev/null)" != "reminder_daemon" ]; then
    echo "Процесс $PID не является демоном reminder_daemon" >&2
    exit 1
fi

if kill -TERM "$PID" 2>/dev/null; then
    echo "Отправлен SIGTERM процессу $PID"
else
    echo "Не удалось отправить SIGTERM процессу $PID" >&2
    exit 1
fi
