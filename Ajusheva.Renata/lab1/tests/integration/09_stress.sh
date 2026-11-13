#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Нагрузка: много событий подряд — ни одно не теряется, и сигналы обрабатываются даже под потоком событий

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

FILE_COUNT=${DISKMOND_TEST_FILES:-1000}
TREE_COUNT=${DISKMOND_TEST_TREES:-200}


await_count() {
    local deadline=$((SECONDS + 30))
    SEEN=0
    while [ "$SECONDS" -lt "$deadline" ]; do
        SEEN=$(tally "$1" "$2")
        if [ "$SEEN" -ge "$3" ]; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

thousand_files() {
    launch_ok || return
    local i
    for ((i = 1; i <= FILE_COUNT; i++)); do
        : >"$DIR_A/file$i"
    done
    await_count local0_lines "create +$(literal "$DIR_A")/file[0-9]+\$" "$FILE_COUNT"
    assert_that "записаны все $FILE_COUNT созданий (вышло $SEEN)" same "$SEEN" "$FILE_COUNT"
    assert_that "очередь inotify ни разу не переполнилась" negated has_line "$CAPTURE" "overflow"
    assert_that "демон жив" alive "$PID"
}

many_trees() {
    launch_ok || return
    local i
    for ((i = 1; i <= TREE_COUNT; i++)); do
        mkdir -p "$DIR_A/t$i/leaf"
        touch "$DIR_A/t$i/leaf/file"
    done
    await_count local0_lines "create +$(literal "$DIR_A")/t[0-9]+/leaf/file\$" "$TREE_COUNT"
    assert_that "файл в глубине записан во всех $TREE_COUNT деревьях (вышло $SEEN)" same "$SEEN" "$TREE_COUNT"
    assert_that "и каждый вложенный каталог — ровно по разу" \
        same "$(tally local0_lines "create +$(literal "$DIR_A")/t[0-9]+/leaf/\$")" "$TREE_COUNT"
}

term_during_flood() {
    launch_ok || return
    (
        for ((i = 1; i <= 50000; i++)); do
            echo "$i" >>"$DIR_A/flood"
        done
    ) &
    HELPERS+=("$!")
    sleep 0.5
    kill -TERM "$PID"
    assert_that "под непрерывным потоком событий SIGTERM обработан за $PATIENCE с" await_exit "$PID"
    assert_that "сообщение о выходе записано" has_line "$CAPTURE" "SIGTERM from pid [0-9]+: exiting\$"
}

begin_suite "Нагрузка"
scenario thousand_files "$FILE_COUNT файлов подряд"
scenario many_trees "$TREE_COUNT деревьев каталогов"
scenario term_during_flood "SIGTERM под потоком событий"
end_suite
