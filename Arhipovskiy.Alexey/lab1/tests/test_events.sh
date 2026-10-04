#!/usr/bin/env bash
# События файлов: запись, чтение, атрибуты, перемещение, удаление, отдельный журнал

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

cookie_of() { events | grep -E -- "$1" | sed -nE 's/.*\(cookie ([0-9]+)\)$/\1/p' | head -n 1; }

same_cookie()
{
    local from to
    from=$(cookie_of "$1")
    to=$(cookie_of "$2")
    [ -n "$from" ] && [ "$from" = "$to" ]
}

test_write_and_read()
{
    require_daemon || return

    echo hello >"$D1/a.txt"
    expect_event "запись: CREATE" "$(ev CREATE file "$D1/a.txt")"
    expect_event "запись: OPEN" "$(ev OPEN file "$D1/a.txt")"
    expect_event "запись: MODIFY" "$(ev MODIFY file "$D1/a.txt")"
    expect_event "запись: CLOSE_WRITE" "$(ev CLOSE_WRITE file "$D1/a.txt")"

    mark_logs
    cat "$D1/a.txt" >/dev/null
    expect_event "чтение: OPEN" "$(ev OPEN file "$D1/a.txt")"
    expect_event "чтение: ACCESS" "$(ev ACCESS file "$D1/a.txt")"
    expect_event "чтение: CLOSE_NOWRITE" "$(ev CLOSE_NOWRITE file "$D1/a.txt")"
    check "при чтении нет MODIFY" not file_has <(events) "$(ev MODIFY file "$D1/a.txt")"

    mark_logs
    ls "$D1" >/dev/null
    expect_event "просмотр каталога: OPEN dir" "$(ev OPEN dir "$D1")"
    expect_event "просмотр каталога: ACCESS dir" "$(ev ACCESS dir "$D1")"
    expect_event "просмотр каталога: CLOSE_NOWRITE dir" "$(ev CLOSE_NOWRITE dir "$D1")"
}

test_attrib_move_delete()
{
    require_daemon || return
    echo data >"$D1/a.txt"
    wait_for events "$(ev CLOSE_WRITE file "$D1/a.txt")"
    mark_logs

    chmod 600 "$D1/a.txt"
    expect_event "chmod: ATTRIB" "$(ev ATTRIB file "$D1/a.txt")"

    mv "$D1/a.txt" "$D2/c.txt"
    expect_event "перемещение: MOVED_FROM из dir1" "$(ev MOVED_FROM file "$D1/a.txt")"
    expect_event "перемещение: MOVED_TO в dir2" "$(ev MOVED_TO file "$D2/c.txt")"
    check "у MOVED_FROM и MOVED_TO одинаковый cookie" \
        same_cookie "$(ev MOVED_FROM file "$D1/a.txt")" "$(ev MOVED_TO file "$D2/c.txt")"

    rm "$D2/c.txt"
    expect_event "удаление: DELETE" "$(ev DELETE file "$D2/c.txt")"
}

test_separate_log_file()
{
    require_daemon || return
    touch "$D1/separate.txt"
    expect_event "событие записано в $EVENTS_FILE" "$(ev CREATE file "$D1/separate.txt")"
    expect_no_service "в $SERVICE_FILE события не дублируются" "$(re "$D1/separate.txt")"
    expect_service "служебные сообщения идут в $SERVICE_FILE" "Disk monitor started \\(pid $DAEMON_PID\\)"
    check "служебных сообщений нет в журнале событий" not file_has <(events) "Disk monitor started"
}

test_unwatched_dir()
{
    require_daemon || return
    touch "$OUT/ignored.txt"
    expect_no_event "файлы вне наблюдаемых каталогов не записываются" "$(re "$OUT")"
}

init_suite "События файлов"
run_test test_write_and_read "Запись и чтение"
run_test test_attrib_move_delete "Атрибуты, перемещение, удаление"
run_test test_separate_log_file "Отдельный журнал LOG_LOCAL0"
run_test test_unwatched_dir "Каталоги вне конфига"
finish
