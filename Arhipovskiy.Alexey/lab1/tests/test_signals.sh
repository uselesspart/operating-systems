#!/usr/bin/env bash
# Сигналы: SIGHUP перечитывает конфиг, SIGTERM завершает демон

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

test_sighup_adds_dir()
{
    local dir3="$TEST_ROOT/dir3"
    mkdir "$dir3"
    require_daemon || return
    touch "$dir3/before.txt"
    expect_no_event "до SIGHUP dir3 не наблюдается" "$(re "$dir3")"

    echo "$dir3" >>"$CONF"
    kill -HUP "$DAEMON_PID"
    expect_service "SIGHUP: конфиг перечитан по абсолютному пути" \
        "disk_monitor\\[$DAEMON_PID\\]: SIGHUP received, re-reading config $(re "$CONF")"
    expect_service "SIGHUP: Config reloaded" "disk_monitor\\[$DAEMON_PID\\]: Config reloaded"
    touch "$dir3/after.txt"
    expect_event "после SIGHUP dir3 наблюдается" "$(ev CREATE file "$dir3/after.txt")"
    check "демон тот же, без перезапуска" equals "$(pid_file_value)" "$DAEMON_PID"
}

test_sighup_removes_dir()
{
    require_daemon || return
    echo "$D1" >"$CONF"
    kill -HUP "$DAEMON_PID"
    expect_service "SIGHUP: Config reloaded" "disk_monitor\\[$DAEMON_PID\\]: Config reloaded"
    mark_logs
    touch "$D2/gone.txt" "$D1/kept.txt"
    expect_event "dir1 из нового конфига наблюдается" "$(ev CREATE file "$D1/kept.txt")"
    expect_no_event "убранный из конфига dir2 больше не наблюдается" "$(re "$D2")"
}

test_sighup_without_config()
{
    require_daemon || return
    rm "$CONF"
    kill -HUP "$DAEMON_PID"
    expect_service "конфиг не читается: прежние настройки сохранены" "Config was not reloaded, keeping previous settings"
    touch "$D1/still.txt"
    expect_event "старые наблюдения работают" "$(ev CREATE file "$D1/still.txt")"
    check "демон продолжает работать" is_running "$DAEMON_PID"
}

test_many_sighups()
{
    require_daemon || return
    local _
    for _ in 1 2 3 4 5; do
        kill -HUP "$DAEMON_PID"
    done
    expect_service "серия SIGHUP обработана" "disk_monitor\\[$DAEMON_PID\\]: Config reloaded"
    check "демон пережил серию SIGHUP" is_running "$DAEMON_PID"
    mark_logs
    touch "$D1/after_burst.txt"
    expect_event "события пишутся и после серии SIGHUP" "$(ev CREATE file "$D1/after_burst.txt")"
}

test_sigterm()
{
    require_daemon || return
    kill -TERM "$DAEMON_PID"
    expect_service "SIGTERM: сообщение о выходе" "disk_monitor\\[$DAEMON_PID\\]: SIGTERM received, exiting"
    expect_service "SIGTERM: Disk monitor stopped" "disk_monitor\\[$DAEMON_PID\\]: Disk monitor stopped"
    expect_event "в журнале событий: monitoring stopped" "disk_monitor\\[$DAEMON_PID\\]: === monitoring stopped"
    check "процесс завершился" wait_exit "$DAEMON_PID"
    check "pid-файл удалён" test ! -e "$PID_FILE"
}

init_suite "Сигналы"
run_test test_sighup_adds_dir "SIGHUP: новый каталог в конфиге"
run_test test_sighup_removes_dir "SIGHUP: каталог убран из конфига"
run_test test_sighup_without_config "SIGHUP: конфиг пропал"
run_test test_many_sighups "SIGHUP несколько раз подряд"
run_test test_sigterm "SIGTERM"
finish
