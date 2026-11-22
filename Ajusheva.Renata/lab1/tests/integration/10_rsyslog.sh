#!/usr/bin/env bash
# Настоящий rsyslog: записи LOG_LOCAL0 оказываются в отдельном файле /var/log/diskmond.log,
# сообщения о самом демоне — в системном журнале. Здесь демон работает без перехвата syslog.
#
# Нужны root, запущенный rsyslog и правило из syslog/ (sudo sh scripts/setup_syslog.sh).
# Если чего-то не хватает, сценарий пропускается и говорит, чего именно.

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

EVENT_LOG=/var/log/diskmond.log
RULE=/etc/rsyslog.d/20-diskmond.conf

size_of() {
    if [ -r "$1" ]; then
        wc -c <"$1" | tr -d ' '
    else
        echo 0
    fi
}


new_in_event_log() { tail -c +"$((EVENT_LOG_START + 1))" "$EVENT_LOG" 2>/dev/null || true; }
new_in_system_log() { tail -c +"$((SYSTEM_LOG_START + 1))" "$SYSTEM_LOG" 2>/dev/null || true; }

real_rsyslog() {
    EVENT_LOG_START=$(size_of "$EVENT_LOG")
    SYSTEM_LOG_START=$(size_of "$SYSTEM_LOG")
    (cd "$START_DIR" && "$DAEMON" -p "$PIDF") >"$WS/stdout.txt" 2>"$WS/stderr.txt"
    PID=$(cat "$PIDF" 2>/dev/null)
    if [ -z "$PID" ]; then
        note_fail "демон не запустился" "$(cat "$WS/stderr.txt")"
        return
    fi
    LAUNCHED+=("$PID")

    local file="$DIR_A/through-rsyslog.txt"
    echo data >"$file"
    expect_line "запись о файле появилась в $EVENT_LOG" new_in_event_log \
        "diskmond\\[$PID\\]: create +$(literal "$file")\$"
    expect_line "и остальные действия с ним тоже" new_in_event_log \
        "diskmond\\[$PID\\]: close-write +$(literal "$file")\$"
    expect_line "сообщение о запуске — в $SYSTEM_LOG" new_in_system_log "diskmond\\[$PID\\]: up and running as pid $PID"
    assert_that "в $SYSTEM_LOG записи о файлах не дублируются" negated has_line <(new_in_system_log) "$(literal "$file")"
    assert_that "в $EVENT_LOG нет сообщений о самом демоне" negated has_line <(new_in_event_log) "up and running"

    kill -TERM "$PID"
    expect_line "сообщение о выходе — в $SYSTEM_LOG" new_in_system_log "diskmond\\[$PID\\]: SIGTERM from pid [0-9]+: exiting\$"
    assert_that "демон завершился" await_exit "$PID"
}

begin_suite "Настоящий rsyslog: отдельный журнал LOG_LOCAL0"

SYSTEM_LOG=""
for candidate in /var/log/syslog /var/log/messages; do
    if [ -f "$candidate" ]; then
        SYSTEM_LOG=$candidate
        break
    fi
done

missing=""
if [ "$(id -u)" -ne 0 ]; then
    missing="нужен root: sudo ./tests/run_all.sh"
elif ! pgrep -x rsyslogd >/dev/null 2>&1; then
    missing="rsyslog не запущен"
elif [ ! -f "$RULE" ]; then
    missing="нет правила $RULE: sudo sh scripts/setup_syslog.sh"
elif [ -z "$SYSTEM_LOG" ]; then
    missing="нет системного журнала /var/log/syslog или /var/log/messages"
fi

if [ -n "$missing" ]; then
    printf '\n'
    note_skip "проверка с настоящим rsyslog — $missing"
else
    scenario real_rsyslog "Записи о файлах в $EVENT_LOG через rsyslog"
fi
end_suite
