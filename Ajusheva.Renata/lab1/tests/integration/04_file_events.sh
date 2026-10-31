#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# События файлов: каждое действие с файлом — отдельная запись в журнале LOG_LOCAL0

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

writing_and_reading() {
    launch_ok || return
    echo hello >"$DIR_A/notes.txt"
    expect_line "новый файл: create" local0_lines "$(entry_re create "$DIR_A/notes.txt")"
    expect_line "новый файл: open" local0_lines "$(entry_re open "$DIR_A/notes.txt")"
    expect_line "новый файл: write" local0_lines "$(entry_re write "$DIR_A/notes.txt")"
    expect_line "новый файл: close-write" local0_lines "$(entry_re close-write "$DIR_A/notes.txt")"

    remember_position
    cat "$DIR_A/notes.txt" >/dev/null
    expect_line "чтение: open" local0_lines "$(entry_re open "$DIR_A/notes.txt")"
    expect_line "чтение: read" local0_lines "$(entry_re read "$DIR_A/notes.txt")"
    expect_line "чтение: close" local0_lines "$(entry_re close "$DIR_A/notes.txt")"
    assert_that "чтение не выдаётся за запись" negated has_line <(local0_lines) "$(entry_re write "$DIR_A/notes.txt")"

    remember_position
    echo more >>"$DIR_A/notes.txt"
    expect_line "дописывание: write" local0_lines "$(entry_re write "$DIR_A/notes.txt")"
    expect_line "дописывание: close-write" local0_lines "$(entry_re close-write "$DIR_A/notes.txt")"
    assert_that "дописывание в существующий файл — без create" \
        negated has_line <(local0_lines) "$(entry_re create "$DIR_A/notes.txt")"

    remember_position
    ls "$DIR_A" >/dev/null
    expect_line "ls по каталогу: open" local0_lines "$(entry_re open "$DIR_A/")"
    expect_line "ls по каталогу: read" local0_lines "$(entry_re read "$DIR_A/")"
    expect_line "ls по каталогу: close" local0_lines "$(entry_re close "$DIR_A/")"
}

entries_in_order() {
    launch_ok || return
    echo data >"$DIR_A/ordered.txt"
    await local0_lines "$(entry_re close-write "$DIR_A/ordered.txt")"
    local actions
    actions=$(local0_lines | grep -F "$DIR_A/ordered.txt" | awk '{print $3}' | tr '\n' ' ')
    assert_that "записи идут в том порядке, в каком всё происходило" same "$actions" "create open write close-write "
}

attributes_and_moves() {
    launch_ok || return
    echo data >"$DIR_A/doc.txt"
    await local0_lines "$(entry_re close-write "$DIR_A/doc.txt")"
    remember_position

    chmod 600 "$DIR_A/doc.txt"
    expect_line "chmod: attrib" local0_lines "$(entry_re attrib "$DIR_A/doc.txt")"
    touch -d '2001-02-03 04:05' "$DIR_A/doc.txt"
    expect_line "смена времени изменения: attrib" local0_lines "$(entry_re attrib "$DIR_A/doc.txt")"

    mv "$DIR_A/doc.txt" "$DIR_A/renamed.txt"
    expect_line "переименование на месте: rename старое -> новое" local0_lines \
        "$(entry_re rename "$DIR_A/doc.txt" "$DIR_A/renamed.txt")"
    mv "$DIR_A/renamed.txt" "$DIR_B/moved.txt"
    expect_line "перенос в другой наблюдаемый каталог: move" local0_lines \
        "$(entry_re move "$DIR_A/renamed.txt" "$DIR_B/moved.txt")"
    assert_that "каждое перемещение — одна запись с обоими путями" negated has_line <(local0_lines) "move-(out|in)"

    rm "$DIR_B/moved.txt"
    expect_line "удаление: delete" local0_lines "$(entry_re delete "$DIR_B/moved.txt")"
}

leaving_and_arriving() {
    echo from-outside >"$OUTSIDE/arrives.txt"
    echo from-inside >"$DIR_A/leaves.txt"
    launch_ok || return
    mv "$DIR_A/leaves.txt" "$OUTSIDE/leaves.txt"
    expect_line "файл унесли из-под наблюдения: move-out" local0_lines "$(entry_re move-out "$DIR_A/leaves.txt")"
    mv "$OUTSIDE/arrives.txt" "$DIR_B/arrives.txt"
    expect_line "файл принесли снаружи: move-in" local0_lines "$(entry_re move-in "$DIR_B/arrives.txt")"
}

links() {
    echo data >"$DIR_A/original"
    launch_ok || return
    ln "$DIR_A/original" "$DIR_A/hardlink"
    expect_line "жёсткая ссылка — это create" local0_lines "$(entry_re create "$DIR_A/hardlink")"
    ln -s original "$DIR_A/symlink"
    expect_line "символическая ссылка — тоже create" local0_lines "$(entry_re create "$DIR_A/symlink")"
}

two_facilities() {
    launch_ok || return
    touch "$DIR_A/f"
    expect_line "события файлов уходят с facility local0" captured \
        "^local0\\.info diskmond\\[$PID\\]: create +$(literal "$DIR_A/f")\$"
    assert_that "сообщения о самом демоне — с facility daemon" has_line "$CAPTURE" \
        "^daemon\\.notice diskmond\\[$PID\\]: up and running"
    assert_that "в daemon событий файлов нет" negated has_line <(daemon_lines) "$(literal "$DIR_A/f")"
    assert_that "в local0 нет сообщений о самом демоне" negated has_line <(local0_lines) "up and running"
}

unwatched_places() {
    launch_ok || return
    echo x >"$OUTSIDE/x"
    cat "$OUTSIDE/x" >/dev/null
    expect_silence "то, что происходит вне каталогов из конфига, не записывается" local0_lines "$(literal "$OUTSIDE")"
}

odd_names() {
    launch_ok || return
   
    local name='100% weird $(name) [x] *.txt'
    touch "$DIR_A/$name"
    expect_line "имя с %, \$, скобками и пробелами записано как есть" local0_lines "$(entry_re create "$DIR_A/$name")"
    touch "$DIR_A/отчёт.txt"
    expect_line "русское имя записано как есть" local0_lines "$(entry_re create "$DIR_A/отчёт.txt")"
    
    touch "$DIR_A/evil"$'\n'"local0.info diskmond[1]: delete forged"
    expect_line "перевод строки в имени экранирован, запись одна" local0_lines \
        "create +$(literal "$DIR_A/evil")\\\\x0Alocal0\\.info diskmond\\[1\\]: delete forged\$"
    assert_that "поддельная запись в журнал не попала" negated has_line "$CAPTURE" "^local0\\.info diskmond\\[1\\]"
}

begin_suite "События файлов"
scenario writing_and_reading "Запись и чтение"
scenario entries_in_order "Порядок записей"
scenario attributes_and_moves "Атрибуты, переименование, перенос, удаление"
scenario leaving_and_arriving "Унесли и принесли"
scenario links "Ссылки"
scenario two_facilities "local0 для событий, daemon для остального"
scenario unwatched_places "Вне наблюдаемых каталогов"
scenario odd_names "Необычные имена"
end_suite
