#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Один экземпляр: pid-файл, проверка через /proc, SIGTERM прежнему демону, чужие процессы не трогаются

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

second_launch_replaces_first() {
    launch_ok || return
    local first=$PID
    launch_ok || return
    local second=$PID

    assert_that "второй запуск дал новый процесс" differ "$first" "$second"
    assert_that "новый процесс нашёл прежний по pid-файлу и /proc и послал ему SIGTERM" has_line "$CAPTURE" \
        "previous instance \\(pid $first\\) is running: sending SIGTERM\$"
    assert_that "прежний записал, что выходит" has_line "$CAPTURE" \
        "diskmond\\[$first\\]: SIGTERM from pid [0-9]+: exiting\$"
    assert_that "новый дождался, пока прежний завершится" has_line "$CAPTURE" \
        "previous instance \\(pid $first\\) has exited\$"
    assert_that "прежнего процесса больше нет" negated alive "$first"
    assert_that "в pid-файле теперь pid нового" same "$(cat "$PIDF")" "$second"
    touch "$DIR_A/after"
    expect_line "события теперь записывает новый процесс" captured \
        "^local0\\.info diskmond\\[$second\\]: create +$(literal "$DIR_A/after")\$"
    assert_that "с этим pid-файлом работает ровно один демон" same "$(pgrep -fc -- "-p $PIDF")" 1
}

stale_pid() {
    sh -c 'exit 0' &
    local gone=$!
    wait "$gone"
    echo "$gone" >"$PIDF"
    launch_ok || return
    assert_that "процесса из pid-файла нет в /proc: файл устарел, запуск идёт дальше" has_line "$CAPTURE" \
        "pid file $(literal "$PIDF") holds pid $gone, but /proc/$gone is absent: the file is stale\$"
    assert_that "pid-файл переписан" same "$(cat "$PIDF")" "$PID"
}

unrelated_process() {
    sleep 60 &
    local stranger=$!
    HELPERS+=("$stranger")
    echo "$stranger" >"$PIDF"
    launch_ok || return
    assert_that "pid достался другой программе — её не трогаем" has_line "$CAPTURE" \
        "holds pid $stranger, which belongs to 'sleep', not to diskmond: not touching it\$"
    assert_that "чужой процесс жив" alive "$stranger"
    assert_that "pid-файл переписан" same "$(cat "$PIDF")" "$PID"
}

impostor_named_diskmond() {
    
    bash -c 'printf diskmond >/proc/$$/comm; while :; do sleep 1; done' &
    local impostor=$!
    HELPERS+=("$impostor")
    local deadline=$((SECONDS + PATIENCE))
    until [ "$(cat "/proc/$impostor/comm" 2>/dev/null)" = diskmond ] || [ "$SECONDS" -ge "$deadline" ]; do
        sleep 0.05
    done
    echo "$impostor" >"$PIDF"
    launch_ok || return
    assert_that "имя совпало, но это другая программа — SIGTERM не посылается" has_line "$CAPTURE" \
        "holds pid $impostor, which belongs to 'diskmond', not to diskmond: not touching it\$"
    assert_that "этот процесс жив" alive "$impostor"
}

garbage_in_pid_file() {
    printf 'definitely not a number\n' >"$PIDF"
    launch_ok || return
    assert_that "мусор в pid-файле запуску не мешает" same "$(cat "$PIDF")" "$PID"
}

foreign_pid_file_survives_exit() {
    launch_ok || return
    echo 999999 >"$PIDF"
    terminate
    assert_that "при выходе демон не удаляет pid-файл, в котором уже не его pid" same "$(cat "$PIDF")" 999999
}

pid_file_unwritable() {
    launch -p "$WS/no/such/dir/diskmond.pid"
    assert_that "код возврата 1" same "$LAUNCH_CODE" 1
    assert_that "запускавший процесс напечатал, почему демон не поднялся" has_line "$WS/stderr.txt" \
        "startup failed: cannot create pid file .*: No such file or directory\$"
    assert_that "причина есть и в системном журнале" has_line "$CAPTURE" "^daemon\\.err .*startup failed"
    sleep 0.3
    assert_that "в фоне ничего не осталось" negated muted pgrep -f -- "$WS/no/such"
}

begin_suite "Один экземпляр (pid-файл)"
scenario second_launch_replaces_first "Повторный запуск"
scenario stale_pid "Устаревший pid"
scenario unrelated_process "pid чужого процесса"
scenario impostor_named_diskmond "Процесс, назвавшийся diskmond"
scenario garbage_in_pid_file "Мусор в pid-файле"
scenario foreign_pid_file_survives_exit "Чужой pid-файл при выходе"
scenario pid_file_unwritable "pid-файл не записать"
end_suite
