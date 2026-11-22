#!/bin/bash
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

usage() {
    echo "Использование: $0 ДАТА ВРЕМЯ [-h|-d|-w] ТЕКСТ" >&2
    echo "  ДАТА  — ГГГГ-ММ-ДД" >&2
    echo "  ВРЕМЯ — ЧЧ:ММ" >&2
    echo "  -h, -d, -w — повтор каждый час, день, неделю" >&2
    exit 1
}

die() {
    echo "add_event: $*" >&2
    exit 1
}

[ $# -ge 3 ] || usage

DATE=$1
TIME=$2
shift 2

FLAG=none
case "${1:-}" in
    -h)
        FLAG=hour
        shift
        ;;
    -d)
        FLAG=day
        shift
        ;;
    -w)
        FLAG=week
        shift
        ;;
    -*)
        die "неизвестный флаг '$1', допустимы -h, -d и -w"
        ;;
esac

[ $# -ge 1 ] || die "не указан текст напоминания"
TEXT=$*

case "$TEXT" in
    *"
"*)
        die "текст не должен содержать перевод строки"
        ;;
esac

if [ -z "$(printf '%s' "$TEXT" | tr -d ' \t')" ]; then
    die "текст напоминания пустой"
fi

case "$DATE" in
    [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]) ;;
    *) die "дата должна быть в формате ГГГГ-ММ-ДД" ;;
esac

case "$TIME" in
    [0-9][0-9]:[0-9][0-9]) ;;
    *) die "время должно быть в формате ЧЧ:ММ" ;;
esac

NORMALIZED=$(date -d "$DATE $TIME" '+%Y-%m-%d %H:%M' 2>/dev/null) || NORMALIZED=""
[ "$NORMALIZED" = "$DATE $TIME" ] || die "несуществующие дата или время: $DATE $TIME"

TIMESTAMP=$(date -d "$DATE $TIME" +%s) || die "не удалось получить timestamp"
[ "$TIMESTAMP" -gt 0 ] || die "дата должна быть позже 1 января 1970 года"

CONFIG=${REMINDER_CONFIG:-"$SCRIPT_DIR/events.conf"}
case "$CONFIG" in
    /*) ;;
    *) CONFIG="$PWD/$CONFIG" ;;
esac

command -v flock >/dev/null 2>&1 || die "не найден flock (util-linux)"
LOCK="$CONFIG.lock"
exec 9>"$LOCK" || die "не удалось открыть $LOCK"
flock 9 || die "не удалось заблокировать $LOCK"

if [ -s "$CONFIG" ] && [ "$(tail -c 1 "$CONFIG" | od -An -t x1 | tr -d ' \n')" != "0a" ]; then
    printf '\n' >> "$CONFIG" || die "не удалось дописать в $CONFIG"
fi

printf '%s %s %s\n' "$TIMESTAMP" "$FLAG" "$TEXT" >> "$CONFIG" || die "не удалось дописать в $CONFIG"

flock -u 9
exec 9>&-
echo "Добавлено событие: $TIMESTAMP $FLAG $TEXT"

if [ -n "${XDG_RUNTIME_DIR:-}" ]; then
    PIDFILE="$XDG_RUNTIME_DIR/reminder_daemon.pid"
else
    PIDFILE="/tmp/reminder_daemon.pid"
fi

if [ -r "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE" 2>/dev/null || true)
    case "$PID" in
        ''|*[!0-9]*) ;;
        *)
            if [ "$(cat "/proc/$PID/comm" 2>/dev/null || true)" = "reminder_daemon" ]; then
                if kill -HUP "$PID" 2>/dev/null; then
                    echo "Демон (pid $PID) перечитает файл событий"
                fi
            fi
            ;;
    esac
fi
