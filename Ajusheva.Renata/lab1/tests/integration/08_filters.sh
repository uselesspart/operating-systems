#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Фильтры: журнал LOG_LOCAL0 внутри наблюдаемого каталога не зацикливает демон; директива ignore

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"


mirror_local0_into() {
    local target=$1 copied=0 total
    while :; do
        total=$(grep -c '^local0\.' "$CAPTURE")
        if [ "$total" -gt "$copied" ]; then
            grep '^local0\.' "$CAPTURE" | tail -n +"$((copied + 1))" | head -n "$((total - copied))" >>"$target"
            copied=$total
        fi
        sleep 0.05
    done
}

log_inside_watched_tree() {
    local log="$DIR_A/diskmond.log"
    echo "logfile $log" >>"$CONF"
    launch_ok || return
    mirror_local0_into "$log" &
    HELPERS+=("$!")

    touch "$DIR_A/trigger"
    expect_line "обычное действие записано" local0_lines "$(entry_re create "$DIR_A/trigger")"
    sleep 1
    assert_that "о самом журнале записей нет" negated has_line <(local0_lines) "$(literal "$log")"
    local before after
    before=$(tally local0_lines '.')
    sleep 1
    after=$(tally local0_lines '.')
    assert_that "журнал не порождает сам себя: записей не прибавляется ($before -> $after)" same "$before" "$after"
    assert_that "а сам журнал действительно пишется в наблюдаемый каталог" test -s "$log"
}

ignored_directory() {
    mkdir -p "$DIR_A/cache/deep"
    echo "ignore $DIR_A/cache" >>"$CONF"
    launch_ok || return
    assert_that "на игнорируемый каталог наблюдение даже не ставится" has_line "$CAPTURE" \
        "watching $(literal "$DIR_A") \\(1 directory\\)\$"
    touch "$DIR_A/cache/a" "$DIR_A/cache/deep/b" "$DIR_A/visible"
    expect_line "обычный файл записан" local0_lines "$(entry_re create "$DIR_A/visible")"
    expect_silence "из игнорируемого — ничего" local0_lines "cache"
}

ignored_file_relative() {
    printf 'watch %s\nignore a/secret.txt\n' "$DIR_A" >"$CONF"
    launch_ok || return
    touch "$DIR_A/secret.txt" "$DIR_A/public.txt"
    expect_line "обычный файл записан" local0_lines "$(entry_re create "$DIR_A/public.txt")"
    expect_silence "файл из ignore (путь относительно конфига) не записан" local0_lines "secret"
}

crossing_ignore_boundary() {
    mkdir -p "$DIR_A/cache"
    echo "ignore $DIR_A/cache" >>"$CONF"
    launch_ok || return
    echo x >"$DIR_A/doc"
    await local0_lines "$(entry_re close-write "$DIR_A/doc")"
    mv "$DIR_A/doc" "$DIR_A/cache/doc"
    expect_line "перенос в игнорируемый каталог записан как move-out" local0_lines "$(entry_re move-out "$DIR_A/doc")"
    mv "$DIR_A/cache/doc" "$DIR_A/doc2"
    expect_line "перенос оттуда — как move-in" local0_lines "$(entry_re move-in "$DIR_A/doc2")"
    mkdir "$DIR_A/cache/sub"
    mv "$DIR_A/cache/sub" "$DIR_A/sub"
    expect_line "каталог, вынесенный из игнорируемого, — move-in" local0_lines "$(entry_re move-in "$DIR_A/sub/")"
    touch "$DIR_A/sub/f"
    expect_line "и теперь он наблюдается" local0_lines "$(entry_re create "$DIR_A/sub/f")"
}

begin_suite "Фильтры"
scenario log_inside_watched_tree "Журнал LOG_LOCAL0 в наблюдаемом каталоге"
scenario ignored_directory "ignore: каталог"
scenario ignored_file_relative "ignore: файл по относительному пути"
scenario crossing_ignore_boundary "Перемещения через границу ignore"
end_suite
