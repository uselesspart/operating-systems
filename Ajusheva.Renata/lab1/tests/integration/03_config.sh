#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Конфигурационный файл: где ищется, ключ -c, относительные пути, разбор строк, ошибки

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

config_missing() {
    rm "$CONF"
    launch
    assert_that "без конфига программа выходит с кодом 1" same "$LAUNCH_CODE" 1
    assert_that "и объясняет это в терминале" has_line "$WS/stderr.txt" \
        "config 'diskmond\\.conf' is not accessible: No such file or directory\$"
    assert_that "то же сообщение ушло в системный журнал как ошибка" has_line "$CAPTURE" \
        "^daemon\\.err diskmond\\[[0-9]+\\]: config 'diskmond\\.conf' is not accessible"
    assert_that "pid-файл не появился" test ! -e "$PIDF"
}

config_from_start_directory() {
    launch_ok || return
    assert_that "использован diskmond.conf из каталога запуска, путь стал абсолютным" \
        has_line "$WS/stderr.txt" "config $(literal "$CONF"),"
    touch "$DIR_A/a"
    expect_line "каталоги из него наблюдаются" local0_lines "$(entry_re create "$DIR_A/a")"
}

option_and_relative_paths() {
    mkdir "$WS/settings"
    printf '# пути отсчитываются от этого файла\nwatch ../a\n' >"$WS/settings/my.conf"
    START_DIR="$WS/settings"
    launch_ok -c my.conf || return
    assert_that "путь из -c превращён в абсолютный" has_line "$WS/stderr.txt" \
        "config $(literal "$WS/settings/my.conf"),"
    touch "$DIR_A/rel"
    expect_line "'../a' понят относительно каталога конфига, а не текущего (/)" \
        local0_lines "$(entry_re create "$DIR_A/rel")"
    touch "$DIR_B/other"
    expect_silence "каталог, которого в этом конфиге нет, не наблюдается" local0_lines "$(literal "$DIR_B")"
}

positional_argument() {
    printf 'watch %s\n' "$DIR_B" >"$WS/second.conf"
    START_DIR=/
    launch_ok "$WS/second.conf" || return
    touch "$DIR_B/pos"
    expect_line "конфиг можно передать просто аргументом" local0_lines "$(entry_re create "$DIR_B/pos")"
}

command_line() {
    "$DAEMON" --help >"$WS/help.txt" 2>&1
    assert_that "--help завершается с кодом 0" same "$?" 0
    assert_that "--help рассказывает про сигналы" has_line "$WS/help.txt" "SIGHUP - re-read the config"
    "$DAEMON" --no-such-option >/dev/null 2>&1
    assert_that "неизвестный ключ: код 2" same "$?" 2
    "$DAEMON" one.conf two.conf >/dev/null 2>&1
    assert_that "два конфига сразу: код 2" same "$?" 2
    "$DAEMON" -c one.conf two.conf >/dev/null 2>&1
    assert_that "-c вместе с позиционным аргументом: код 2" same "$?" 2
}

line_syntax() {
    mkdir -p "$DIR_A/nested" "$WS/with space"
    touch "$WS/plain.txt"
    {
        echo "# комментарий"               
        echo                               
        echo "   "                         
        printf 'watch %s\r\n' "$DIR_B"     
        echo "  WATCH   =   $DIR_A  "      
        echo "watch $DIR_A"                
        echo "watch $DIR_A/nested"         
        echo "watch $WS/nowhere"           
        echo "watch $WS/plain.txt"         
        echo "watch with space"            
        echo "frobnicate /x"               
    } >"$CONF"
    launch_ok || return
    expect_line "строка 8: несуществующий каталог — предупреждение с номером строки" daemon_lines \
        "^daemon\\.warning .*diskmond\\.conf:8: cannot use '.*nowhere': No such file or directory \\(line ignored\\)\$"
    expect_line "строка 9: файл вместо каталога" daemon_lines \
        "diskmond\\.conf:9: '.*plain\\.txt' is not a directory \\(line ignored\\)\$"
    expect_line "строка 6: повтор не нужен" daemon_lines \
        "diskmond\\.conf:6: '$(literal "$DIR_A")' is already listed \\(line ignored\\)\$"
    expect_line "строка 7: вложенный каталог и так наблюдается" daemon_lines \
        "diskmond\\.conf:7: '$(literal "$DIR_A/nested")' is inside '$(literal "$DIR_A")'"
    expect_line "строка 11: неизвестное слово" daemon_lines "diskmond\\.conf:11: unknown keyword 'frobnicate'"
    expect_line "строка 4: CRLF не мешает" daemon_lines "watching $(literal "$DIR_B") \\(1 directory\\)\$"
    expect_line "строка 5: регистр, '=' и пробелы не мешают" daemon_lines \
        "watching $(literal "$DIR_A") \\(2 directories\\)\$"
    expect_line "строка 10: путь с пробелом" daemon_lines "watching $(literal "$WS/with space") \\(1 directory\\)\$"
    assert_that "замечания по конфигу видны и в терминале" has_line "$WS/stderr.txt" "unknown keyword"

    touch "$DIR_A/nested/once"
    expect_line "событие во вложенном каталоге записано" local0_lines "$(entry_re create "$DIR_A/nested/once")"
    sleep "$SILENCE"
    assert_that "и записано один раз, хотя каталог упомянут дважды" \
        same "$(tally local0_lines "$(entry_re create "$DIR_A/nested/once")")" 1
}

nothing_to_watch() {
    printf '# пока ни одного каталога\n' >"$CONF"
    launch_ok || return
    assert_that "предупреждение: наблюдать нечего, ждём SIGHUP" has_line "$CAPTURE" \
        "^daemon\\.warning .*nothing to watch: $(literal "$CONF") lists no usable directories"
    assert_that "демон при этом работает" alive "$PID"
}

config_is_directory() {
    mkdir "$WS/folder.conf"
    launch -c "$WS/folder.conf"
    assert_that "каталог вместо конфига: код 1" same "$LAUNCH_CODE" 1
    assert_that "понятная причина" has_line "$WS/stderr.txt" "is not a regular file"
}

begin_suite "Конфигурационный файл"
scenario config_missing "Конфига нет"
scenario config_from_start_directory "Конфиг из каталога запуска"
scenario option_and_relative_paths "Ключ -c и относительные пути"
scenario positional_argument "Конфиг аргументом"
scenario command_line "Командная строка"
scenario line_syntax "Разбор строк"
scenario nothing_to_watch "Пустой конфиг"
scenario config_is_directory "Каталог вместо файла"
end_suite
