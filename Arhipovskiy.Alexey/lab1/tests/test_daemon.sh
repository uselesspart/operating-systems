#!/usr/bin/env bash
# Демонизация: фоновый процесс без терминала, рабочий каталог /, pid-файл

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

test_runs_in_background()
{
    start_daemon
    check "команда запуска сразу вернула код 0" equals "$START_STATUS" 0
    check "демон работает, его pid записан в $PID_FILE" test -n "$DAEMON_PID"
    [ -n "$DAEMON_PID" ] || return

    check "в терминал выведен абсолютный путь к конфигу" \
        file_has "$TEST_ROOT/start.out" "Starting daemon: config $(re "$CONF")"
    check "pid-файл содержит pid демона" equals "$(pid_file_value)" "$DAEMON_PID"
    expect_service "в системном журнале: Disk monitor started" \
        "disk_monitor\\[$DAEMON_PID\\]: Disk monitor started \\(pid $DAEMON_PID\\)"
}

test_detached_from_terminal()
{
    require_daemon || return
    local sid
    sid=$(ps -o sid= -p "$DAEMON_PID" | tr -d ' ')

    check "нет управляющего терминала (TTY = ?)" equals "$(ps -o tty= -p "$DAEMON_PID" | tr -d ' ')" "?"
    check "свой сеанс (setsid), не сеанс теста" not equals "$sid" "$(ps -o sid= -p $$ | tr -d ' ')"
    check "демон не лидер сеанса (второй fork)" not equals "$sid" "$DAEMON_PID"
    check "рабочий каталог /" equals "$(readlink "/proc/$DAEMON_PID/cwd")" /

    local fd
    for fd in 0 1 2; do
        check "дескриптор $fd направлен в /dev/null" equals "$(readlink "/proc/$DAEMON_PID/fd/$fd")" /dev/null
    done
    if grep -q '^Umask:' "/proc/$DAEMON_PID/status"; then
        check "umask 0" equals "$(awk '/^Umask:/ {print $2}' "/proc/$DAEMON_PID/status")" 0000
    fi
}

test_sleeps_when_idle()
{
    require_daemon || return
    sleep 1
    check "без событий демон спит (состояние S)" equals "$(process_state "$DAEMON_PID")" S
    check "демон продолжает работать" is_running "$DAEMON_PID"
}

init_suite "Демонизация"
run_test test_runs_in_background "Запуск в фоне"
run_test test_detached_from_terminal "Отрыв от терминала"
run_test test_sleeps_when_idle "Работа без интервала"
finish
