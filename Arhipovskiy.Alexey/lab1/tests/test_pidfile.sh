#!/usr/bin/env bash
# pid-файл: повторный запуск, проверка через /proc, устаревший и чужой pid

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

test_restart_stops_old()
{
    require_daemon || return
    local old=$DAEMON_PID
    require_daemon || return
    local new=$DAEMON_PID

    check "в терминале: путь к pid-файлу $PID_FILE" file_has "$TEST_ROOT/start.out" "pid file $(re "$PID_FILE")"
    expect_service "новый процесс нашёл старый и послал SIGTERM" \
        "Daemon is already running \\(pid $old\\), sending SIGTERM"
    expect_service "старый демон записал выход" "disk_monitor\\[$old\\]: SIGTERM received, exiting"
    check "старый демон завершился" wait_exit "$old"
    check "новый демон работает" is_running "$new"
    check "в pid-файле pid нового демона" equals "$(pid_file_value)" "$new"
}

test_stale_pid()
{
    sh -c 'exit 0' &
    local dead=$!
    wait "$dead"
    echo "$dead" >"$PID_FILE"
    require_daemon || return
    expect_service "процесса из pid-файла нет в /proc: файл устарел" \
        "Stale pid file $(re "$PID_FILE"): process $dead does not exist"
    check "pid-файл перезаписан" equals "$(pid_file_value)" "$DAEMON_PID"
}

test_foreign_pid()
{
    sleep 60 &
    local foreign=$!
    HELPER_PIDS+=("$foreign")
    echo "$foreign" >"$PID_FILE"
    require_daemon || return
    expect_service "pid принадлежит другой программе: её не трогаем" \
        "pid $foreign belongs to 'sleep', not touching it"
    check "чужой процесс не получил SIGTERM" is_running "$foreign"
    check "pid-файл перезаписан" equals "$(pid_file_value)" "$DAEMON_PID"
}

test_garbage_pid()
{
    echo "not a pid" >"$PID_FILE"
    require_daemon || return
    check "мусор в pid-файле не мешает запуску" equals "$(pid_file_value)" "$DAEMON_PID"
}

test_keeps_replaced_pid_file()
{
    require_daemon || return
    echo 999999 >"$PID_FILE"
    stop_daemon
    check "чужой pid-файл не удалён при выходе" equals "$(pid_file_value)" 999999
}

init_suite "pid-файл"
run_test test_restart_stops_old "Повторный запуск"
run_test test_stale_pid "Устаревший pid"
run_test test_foreign_pid "pid чужого процесса"
run_test test_garbage_pid "Мусор в pid-файле"
run_test test_keeps_replaced_pid_file "Удаление только своего pid-файла"
finish
