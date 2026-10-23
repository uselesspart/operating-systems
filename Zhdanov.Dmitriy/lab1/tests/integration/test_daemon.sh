#!/bin/bash
set -u

BIN=${1:-}
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "Использование: $0 путь-к-reminder_daemon" >&2
    exit 2
fi
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")

SCRIPT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
ADD_EVENT="$SCRIPT_DIR/add_event.sh"

TMP=$(mktemp -d)
CONFIG="$TMP/events.conf"
PIDFILE="$TMP/reminder_daemon.pid"
LOG="$TMP/terminal.log"
STUB="$TMP/terminal"

export REMINDER_CONFIG="$CONFIG"
export REMINDER_TERMINAL="$STUB"
export REMINDER_TERMINAL_LOG="$LOG"
export XDG_RUNTIME_DIR="$TMP"

FAILED=0

fail() {
    echo "FAIL: $*" >&2
    FAILED=1
}

process_alive() {
    local pid=$1
    local stat
    stat=$(cat "/proc/$pid/stat" 2>/dev/null) || return 1
    stat=${stat##*) }
    case "$stat" in
        Z*) return 1 ;;
        *) return 0 ;;
    esac
}
cleanup() {
    if [ -r "$PIDFILE" ]; then
        PID=$(cat "$PIDFILE" 2>/dev/null || true)
        if [ -n "$PID" ]; then
            kill -TERM "$PID" 2>/dev/null || true
            for _ in $(seq 1 30); do
                process_alive "$PID" || break
                sleep 0.1
            done
        fi
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

cat > "$STUB" <<'STUB_END'
#!/bin/sh
printf '%s\n' "$@" >> "$REMINDER_TERMINAL_LOG"
STUB_END
chmod +x "$STUB"

wait_for_text() {
    local file=$1
    local text=$2
    for _ in $(seq 1 100); do
        if [ -f "$file" ] && grep -qF "$text" "$file"; then
            return 0
        fi
        sleep 0.1
    done
    return 1
}

NOW=$(date +%s)
{
    echo "$((NOW - 3)) none Срочное напоминание"
    echo "$((NOW - 5)) hour Каждый час"
} > "$CONFIG"

"$BIN" >/dev/null 2>"$TMP/start.err" || fail "демон не запустился"

for _ in $(seq 1 50); do
    if [ -s "$PIDFILE" ]; then
        break
    fi
    sleep 0.1
done
if [ ! -s "$PIDFILE" ]; then
    fail "PID-файл не создан"
    cat "$TMP/start.err" >&2
    exit 1
fi

PID=$(cat "$PIDFILE")
process_alive "$PID" || fail "процесс $PID не работает"

wait_for_text "$LOG" "Срочное напоминание" || fail "одноразовое напоминание не показано"
wait_for_text "$LOG" "Каждый час" || fail "повторяющееся напоминание не показано"
grep -qF "Срочное напоминание" "$CONFIG" && fail "одноразовое событие осталось в файле"

for _ in $(seq 1 50); do
    if ! grep -qF "Каждый час" "$CONFIG"; then
        break
    fi
    sleep 0.1
done
if grep -qF "Каждый час" "$CONFIG"; then
    TIMESTAMP=$(awk '/Каждый час/ {print $1}' "$CONFIG")
    [ "$TIMESTAMP" -gt "$(date +%s)" ] || fail "повторяющееся событие не сдвинуто в будущее"
else
    fail "повторяющееся событие пропало из файла"
fi

kill -HUP "$PID" 2>/dev/null || fail "не удалось отправить SIGHUP"
sleep 0.3
process_alive "$PID" || fail "демон завершился после SIGHUP"

EVENT_DATE=$(date -d "-1 minute" +%Y-%m-%d)
EVENT_TIME=$(date -d "-1 minute" +%H:%M)
bash "$ADD_EVENT" "$EVENT_DATE" "$EVENT_TIME" "Событие после SIGHUP" >/dev/null || fail "add_event не добавил событие"
wait_for_text "$LOG" "Событие после SIGHUP" || fail "событие после SIGHUP не показано"
sleep 0.3
grep -qF "Событие после SIGHUP" "$CONFIG" && fail "одноразовое событие после SIGHUP осталось в файле"

OLD_PID=$PID
"$BIN" >/dev/null 2>&1 || fail "повторный запуск завершился с ошибкой"
for _ in $(seq 1 100); do
    NEW_PID=$(cat "$PIDFILE" 2>/dev/null || true)
    if [ -n "$NEW_PID" ] && [ "$NEW_PID" != "$OLD_PID" ] && process_alive "$NEW_PID"; then
        break
    fi
    sleep 0.1
done
NEW_PID=$(cat "$PIDFILE" 2>/dev/null || true)
if [ -z "$NEW_PID" ] || [ "$NEW_PID" = "$OLD_PID" ]; then
    fail "новый экземпляр не заменил старый"
elif ! process_alive "$NEW_PID"; then
    fail "новый экземпляр не работает"
fi
if process_alive "$OLD_PID"; then
    fail "старый экземпляр не завершился"
fi

kill -TERM "$NEW_PID" 2>/dev/null || fail "не удалось отправить SIGTERM"
for _ in $(seq 1 50); do
    process_alive "$NEW_PID" || break
    sleep 0.1
done
process_alive "$NEW_PID" && fail "демон не завершился по SIGTERM"
[ -e "$PIDFILE" ] && fail "PID-файл не удалён"

FAIL_CONFIG="$TMP/fail-events.conf"
printf '%s none Без терминала\n' "$(date +%s)" > "$FAIL_CONFIG"
export REMINDER_CONFIG="$FAIL_CONFIG"
export REMINDER_TERMINAL="$TMP/no-such-terminal"

"$BIN" >/dev/null 2>&1 || fail "демон не запустился для проверки ошибки терминала"
for _ in $(seq 1 50); do
    [ -s "$PIDFILE" ] && break
    sleep 0.1
done
if [ ! -s "$PIDFILE" ]; then
    fail "PID-файл не создан для проверки ошибки терминала"
else
    FAIL_PID=$(cat "$PIDFILE")
    sleep 1.5
    process_alive "$FAIL_PID" || fail "демон завершился при ошибке терминала"
    grep -qF "Без терминала" "$FAIL_CONFIG" || fail "событие удалено при ошибке терминала"
    kill -TERM "$FAIL_PID" 2>/dev/null || fail "не удалось отправить SIGTERM после проверки терминала"
    for _ in $(seq 1 50); do
        process_alive "$FAIL_PID" || break
        sleep 0.1
    done
    process_alive "$FAIL_PID" && fail "демон не завершился после проверки терминала"
    [ -e "$PIDFILE" ] && fail "PID-файл не удалён после проверки терминала"
fi

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "OK"
