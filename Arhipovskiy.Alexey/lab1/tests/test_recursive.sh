#!/usr/bin/env bash
# Подкаталоги: рекурсивное наблюдение, новые, переименованные и перемещённые каталоги

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
=
wait_dir_event()
{
    wait_for events "$(ev "$1" dir "$2")"
    sleep 0.3
}

test_existing_subdirs()
{
    mkdir -p "$D1/a/b"
    require_daemon || return
    expect_service "подкаталоги посчитаны при запуске" "Watching $(re "$D1"): 3 dir"
    touch "$D1/a/b/deep.txt"
    expect_event "событие в подкаталоге 2-го уровня" "$(ev CREATE file "$D1/a/b/deep.txt")"
}

test_new_subdir()
{
    require_daemon || return
    mkdir "$D1/new"
    expect_event "создание каталога: CREATE dir" "$(ev CREATE dir "$D1/new")"
    wait_dir_event CREATE "$D1/new"
    touch "$D1/new/f.txt"
    expect_event "новый каталог сразу наблюдается" "$(ev CREATE file "$D1/new/f.txt")"

    mkdir -p "$D1/p/q/r"
    wait_dir_event CREATE "$D1/p"
    touch "$D1/p/q/r/deep.txt"
    expect_event "mkdir -p: наблюдается всё созданное дерево" "$(ev CREATE file "$D1/p/q/r/deep.txt")"
}

test_no_duplicates()
{
    mkdir "$D1/sub"
    require_daemon || return
    ls "$D1/sub" >/dev/null
    wait_for events "$(ev OPEN dir "$D1/sub")"
    sleep "$QUIET_PERIOD"
    check "открытие подкаталога записано один раз" equals "$(count_lines events "$(ev OPEN dir "$D1/sub")")" 1

    mark_logs
    mkdir "$D1/empty"
    wait_dir_event CREATE "$D1/empty"
    expect_no_event "обход нового каталога самим демоном не записан" "$(ev OPEN dir "$D1/empty")"
}

test_rename_subdir()
{
    mkdir "$D1/old"
    require_daemon || return
    mv "$D1/old" "$D1/renamed"
    expect_event "переименование: MOVED_FROM dir" "$(ev MOVED_FROM dir "$D1/old")"
    expect_event "переименование: MOVED_TO dir" "$(ev MOVED_TO dir "$D1/renamed")"
    wait_dir_event MOVED_TO "$D1/renamed"
    touch "$D1/renamed/g.txt"
    expect_event "события идут с новым путём" "$(ev CREATE file "$D1/renamed/g.txt")"
    check "старый путь больше не используется" not file_has <(events) "$(re "$D1/old/g.txt")"
}

test_move_out_and_in()
{
    mkdir "$D1/moving"
    require_daemon || return

    mv "$D1/moving" "$OUT/moving"
    expect_event "каталог унесён наружу: MOVED_FROM dir" "$(ev MOVED_FROM dir "$D1/moving")"
    sleep 0.3
    touch "$OUT/moving/h.txt"
    expect_no_event "в унесённом каталоге события не пишутся" "h\\.txt"

    mv "$OUT/moving" "$D2/moving"
    expect_event "каталог принесён внутрь: MOVED_TO dir" "$(ev MOVED_TO dir "$D2/moving")"
    wait_dir_event MOVED_TO "$D2/moving"
    touch "$D2/moving/x.txt"
    expect_event "принесённый каталог наблюдается" "$(ev CREATE file "$D2/moving/x.txt")"
}

test_watched_root_removed()
{
    require_daemon || return
    rm -rf "$D2"
    expect_service "удаление каталога из конфига: предупреждение" \
        "Watched directory $(re "$D2") was moved or removed"
    touch "$D1/still.txt"
    expect_event "остальные каталоги наблюдаются дальше" "$(ev CREATE file "$D1/still.txt")"
}

init_suite "Подкаталоги"
run_test test_existing_subdirs "Подкаталоги при запуске"
run_test test_new_subdir "Новые подкаталоги"
run_test test_no_duplicates "Без дублей и лишних событий"
run_test test_rename_subdir "Переименование подкаталога"
run_test test_move_out_and_in "Перемещение наружу и внутрь"
run_test test_watched_root_removed "Удаление каталога из конфига"
finish
