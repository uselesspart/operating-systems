#!/usr/bin/env bash
# Конфигурационный файл: поиск в рабочем каталоге, аргумент, формат строк

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

test_missing_config()
{
    rm "$CONF"
    launch
    check "без конфига код возврата 1" equals "$START_STATUS" 1
    check "в терминале сообщение «not found»" \
        file_has "$TEST_ROOT/start.out" "Config file 'disk_monitor.conf' not found"
    check "демон не запущен" equals "$(pid_file_value)" ""
}

test_config_from_working_dir()
{
    require_daemon || return
    check "конфиг взят из рабочего каталога" \
        file_has "$TEST_ROOT/start.out" "config $(re "$CONF")"
    touch "$D1/a.txt"
    expect_event "наблюдается каталог из конфига" "$(ev CREATE file "$D1/a.txt")"
}

test_config_argument_and_relative_path()
{
    mkdir "$TEST_ROOT/conf"
    printf '# относительный путь\n../dir1\n' >"$TEST_ROOT/conf/my.conf"
    # shellcheck disable=SC2034
    RUN_DIR=/
    launch conf_does_not_exist_here
    check "конфиг из аргумента ищется от рабочего каталога (/)" equals "$START_STATUS" 1

    require_daemon "$TEST_ROOT/conf/my.conf" || return
    check "путь из аргумента стал абсолютным" \
        file_has "$TEST_ROOT/start.out" "config $(re "$TEST_ROOT/conf/my.conf")"
    touch "$D1/rel.txt"
    expect_event "относительный путь взят от каталога конфига, а не от /" "$(ev CREATE file "$D1/rel.txt")"
    touch "$D2/other.txt"
    expect_no_event "каталог не из конфига не наблюдается" "$(re "$D2")"
}

test_line_format()
{
    mkdir -p "$D1/nested"
    touch "$TEST_ROOT/plain.txt"
    {
        echo "# комментарий"
        echo ""
        echo "   "
        printf '%s\r\n' "$D2"
        echo "  $D1  "
        echo "$D1"
        echo "$D1/nested"
        echo "$TEST_ROOT/missing"
        echo "$TEST_ROOT/plain.txt"
    } >"$CONF"
    require_daemon || return

    expect_service "несуществующий каталог пропущен с предупреждением" \
        "'$(re "$TEST_ROOT/missing")' skipped: No such file or directory"
    expect_service "файл вместо каталога пропущен" "'$(re "$TEST_ROOT/plain.txt")' skipped: not a directory"
    expect_service "повтор каталога отброшен" "'$(re "$D1")' is already covered"
    expect_service "вложенный каталог отброшен" "'$(re "$D1/nested")' is already covered"
    expect_service "строка с CRLF (Windows) понята" "Watching $(re "$D2"): 1 dir"
    expect_service "пробелы по краям строки отброшены" "Watching $(re "$D1"): 2 dir"

    touch "$D1/nested/once.txt"
    expect_event "событие во вложенном каталоге записано" "$(ev CREATE file "$D1/nested/once.txt")"
    sleep "$QUIET_PERIOD"
    check "и записано один раз, без дубля" equals "$(count_lines events "$(ev CREATE file "$D1/nested/once.txt")")" 1
}

test_empty_config()
{
    printf '# только комментарий\n' >"$CONF"
    require_daemon || return
    expect_service "предупреждение: нечего наблюдать до SIGHUP" "nothing to watch until SIGHUP"
    check "демон всё равно работает" is_running "$DAEMON_PID"
}

init_suite "Конфигурационный файл"
run_test test_missing_config "Конфиг не найден"
run_test test_config_from_working_dir "Конфиг из рабочего каталога"
run_test test_config_argument_and_relative_path "Конфиг из аргумента, относительные пути"
run_test test_line_format "Формат строк конфига"
run_test test_empty_config "Пустой конфиг"
finish
