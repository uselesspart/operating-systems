#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Подкаталоги: наблюдение на всю глубину, новые, переименованные, перенесённые и удалённые каталоги

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"


exactly_once() {
    assert_that "$1" same "$(tally local0_lines "$2")" 1
}

deep_tree_at_start() {
    mkdir -p "$DIR_A/x/y/z"
    launch_ok || return
    assert_that "при запуске найдены все уровни вложенности" has_line "$CAPTURE" \
        "watching $(literal "$DIR_A") \\(4 directories\\)\$"
    touch "$DIR_A/x/y/z/deep"
    expect_line "файл на третьем уровне виден" local0_lines "$(entry_re create "$DIR_A/x/y/z/deep")"
}

new_directory() {
    launch_ok || return
    mkdir "$DIR_A/fresh"
    expect_line "mkdir: create с косой чертой на конце" local0_lines "$(entry_re create "$DIR_A/fresh/")"
    touch "$DIR_A/fresh/f"
    expect_line "в новом каталоге действия видны сразу" local0_lines "$(entry_re create "$DIR_A/fresh/f")"
}

mkdir_parents() {
    launch_ok || return
    mkdir -p "$DIR_A/p/q/r"
    expect_line "mkdir -p: create p/" local0_lines "$(entry_re create "$DIR_A/p/")"
    
    expect_line "mkdir -p: create p/q/" local0_lines "$(entry_re create "$DIR_A/p/q/")"
    expect_line "mkdir -p: create p/q/r/" local0_lines "$(entry_re create "$DIR_A/p/q/r/")"
    touch "$DIR_A/p/q/r/leaf"
    expect_line "под наблюдением всё созданное дерево" local0_lines "$(entry_re create "$DIR_A/p/q/r/leaf")"
    sleep "$SILENCE"
    local level
    for level in p/ p/q/ p/q/r/; do
        exactly_once "create $level записан один раз" "$(entry_re create "$DIR_A/$level")"
    done
}

copied_tree() {
    mkdir -p "$OUTSIDE/src/m/n"
    echo 1 >"$OUTSIDE/src/m/n/deep.txt"
    echo 2 >"$OUTSIDE/src/top.txt"
    launch_ok || return
    cp -r "$OUTSIDE/src" "$DIR_A/copy"
    expect_line "cp -r: сам каталог" local0_lines "$(entry_re create "$DIR_A/copy/")"
    expect_line "cp -r: вложенный каталог" local0_lines "$(entry_re create "$DIR_A/copy/m/n/")"
    expect_line "cp -r: файл в глубине" local0_lines "$(entry_re create "$DIR_A/copy/m/n/deep.txt")"
    expect_line "cp -r: файл в корне копии" local0_lines "$(entry_re create "$DIR_A/copy/top.txt")"
    sleep "$SILENCE"
    local item
    for item in copy/ copy/m/ copy/m/n/ copy/m/n/deep.txt copy/top.txt; do
        exactly_once "create $item — один раз" "$(entry_re create "$DIR_A/$item")"
    done
    touch "$DIR_A/copy/m/n/later"
    expect_line "скопированное дерево под наблюдением" local0_lines "$(entry_re create "$DIR_A/copy/m/n/later")"
}

own_scans_are_silent() {
    mkdir -p "$DIR_A/sub/inner"
    launch_ok || return
    assert_that "обход каталогов при запуске в журнал не попал" \
        negated has_line <(local0_lines) " (open|read|close) +.*/\$"
    remember_position
    
    mkdir "$DIR_A/new"
    mkdir "$DIR_A/new/deeper"
    await local0_lines "$(entry_re create "$DIR_A/new/deeper/")"
    expect_silence "обход нового каталога демоном в журнал не попал" local0_lines " (open|read|close) +.*/\$"
    ls "$DIR_A/sub" >/dev/null
    expect_line "а ls пользователя записан" local0_lines "$(entry_re open "$DIR_A/sub/")"
    sleep "$SILENCE"
    exactly_once "и записан один раз, хотя сообщают о нём и сам каталог, и родитель" \
        "$(entry_re open "$DIR_A/sub/")"
}

renamed_directory() {
    mkdir -p "$DIR_A/before/inner"
    launch_ok || return
    mv "$DIR_A/before" "$DIR_A/after"
    expect_line "переименование каталога: одна запись rename" local0_lines \
        "$(entry_re rename "$DIR_A/before/" "$DIR_A/after/")"
    touch "$DIR_A/after/inner/g"
    expect_line "внутри переименованного каталога пути уже новые" local0_lines "$(entry_re create "$DIR_A/after/inner/g")"
    assert_that "старый путь больше не встречается" negated has_line <(local0_lines) "$(literal "$DIR_A/before/inner")"
}

directory_between_roots() {
    mkdir -p "$DIR_A/crate/inner"
    launch_ok || return
    mv "$DIR_A/crate" "$DIR_B/crate"
    expect_line "перенос каталога в другой корень: move" local0_lines \
        "$(entry_re move "$DIR_A/crate/" "$DIR_B/crate/")"
    touch "$DIR_B/crate/inner/h"
    expect_line "наблюдение переехало вместе с каталогом" local0_lines "$(entry_re create "$DIR_B/crate/inner/h")"
}

directory_out_and_back() {
    mkdir -p "$DIR_A/trip/inner"
    launch_ok || return
    mv "$DIR_A/trip" "$OUTSIDE/trip"
    expect_line "каталог унесли из-под наблюдения: move-out" local0_lines "$(entry_re move-out "$DIR_A/trip/")"
    touch "$OUTSIDE/trip/inner/x"
    expect_silence "в унесённом каталоге ничего не записывается" local0_lines "trip/inner/x"

    mv "$OUTSIDE/trip" "$DIR_B/trip"
    expect_line "каталог принесли обратно: move-in" local0_lines "$(entry_re move-in "$DIR_B/trip/")"
    touch "$DIR_B/trip/inner/y"
    expect_line "принесённый каталог наблюдается со всеми вложенными" local0_lines \
        "$(entry_re create "$DIR_B/trip/inner/y")"
    assert_that "старое содержимое принесённого каталога не выдаётся за созданное" \
        negated has_line <(local0_lines) "$(entry_re create "$DIR_B/trip/inner/x")"
}

removed_tree() {
    mkdir -p "$DIR_A/old/deep"
    touch "$DIR_A/old/deep/f"
    launch_ok || return
    rm -rf "$DIR_A/old"
    expect_line "rm -rf: delete файла" local0_lines "$(entry_re delete "$DIR_A/old/deep/f")"
    expect_line "rm -rf: delete вложенного каталога" local0_lines "$(entry_re delete "$DIR_A/old/deep/")"
    expect_line "rm -rf: delete самого каталога" local0_lines "$(entry_re delete "$DIR_A/old/")"
    mkdir "$DIR_A/old"
    touch "$DIR_A/old/again"
    expect_line "каталог с тем же именем снова наблюдается" local0_lines "$(entry_re create "$DIR_A/old/again")"
}

root_deleted() {
    launch_ok || return
    rm -rf "$DIR_B"
    expect_line "удалён каталог из конфига: delete-self" local0_lines "$(entry_re delete-self "$DIR_B/")"
    expect_line "и предупреждение в системный журнал" daemon_lines "watched root $(literal "$DIR_B") is gone"
    touch "$DIR_A/still"
    expect_line "второй каталог по-прежнему наблюдается" local0_lines "$(entry_re create "$DIR_A/still")"
    assert_that "демон продолжает работать" alive "$PID"
}

root_moved() {
    launch_ok || return
    mv "$DIR_B" "$OUTSIDE/b-moved"
    expect_line "каталог из конфига перемещён: move-self" local0_lines "$(entry_re move-self "$DIR_B/")"
    expect_line "и предупреждение в системный журнал" daemon_lines "watched root $(literal "$DIR_B") was moved away"
    touch "$OUTSIDE/b-moved/x"
    expect_silence "на новом месте за ним больше не следим" local0_lines "b-moved"
}

symlinked_directory() {
    mkdir "$OUTSIDE/target"
    ln -s "$OUTSIDE/target" "$DIR_A/shortcut"
    launch_ok || return
    assert_that "ссылка на каталог не считается подкаталогом" has_line "$CAPTURE" \
        "watching $(literal "$DIR_A") \\(1 directory\\)\$"
    touch "$OUTSIDE/target/via-link"
    expect_silence "наблюдение не уходит по ссылке наружу" local0_lines "via-link"
}

begin_suite "Подкаталоги"
scenario deep_tree_at_start "Глубокое дерево при запуске"
scenario new_directory "Новый каталог"
scenario mkdir_parents "mkdir -p"
scenario copied_tree "Скопированное дерево (cp -r)"
scenario own_scans_are_silent "Обход каталогов демоном не записывается"
scenario renamed_directory "Переименование каталога"
scenario directory_between_roots "Перенос каталога между корнями"
scenario directory_out_and_back "Каталог унесли и принесли"
scenario removed_tree "Удаление дерева"
scenario root_deleted "Удалён каталог из конфига"
scenario root_moved "Перемещён каталог из конфига"
scenario symlinked_directory "Символическая ссылка на каталог"
end_suite
