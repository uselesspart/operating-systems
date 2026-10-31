#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Демонизация: процесс в фоне, без терминала, в своём сеансе, в каталоге /, с /dev/null вместо stdin/out/err

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

goes_to_background() {
    launch_ok || return
    assert_that "команда запуска вернула 0" same "$LAUNCH_CODE" 0
    assert_that "и напечатала pid процесса в фоне" has_line "$WS/stdout.txt" "running in background, pid $PID\$"
    assert_that "до ухода в фон показала, какой конфиг и pid-файл взяты (абсолютные пути)" \
        has_line "$WS/stderr.txt" "launching in background: config $(literal "$CONF"), pid file $(literal "$PIDF")\$"
    assert_that "процесс жив после возврата команды" alive "$PID"
    assert_that "процесс называется diskmond" same "$(cat "/proc/$PID/comm")" diskmond
    assert_that "pid в pid-файле — того процесса, что остался в фоне" same "$(cat "$PIDF")" "$PID"
    # Команда возвращает управление, когда демон готов, поэтому запись о запуске уже есть
    assert_that "запуск записан в системный журнал с facility daemon" has_line "$CAPTURE" \
        "^daemon\\.notice diskmond\\[$PID\\]: up and running as pid $PID, 2 directories under 2 roots\$"
    assert_that "журнал событий открыт отметкой <start>" has_line "$CAPTURE" \
        "^local0\\.info diskmond\\[$PID\\]: <start> 2 directories under 2 roots\$"
}

no_terminal_own_session() {
    launch_ok || return
    local state ppid pgrp session tty my_session
    read -r state ppid pgrp session tty _ <<<"$(stat_fields "$PID")"
    my_session=$(stat_fields $$ | awk '{print $4}')

    assert_that "управляющего терминала нет (tty_nr = 0)" same "$tty" 0
    assert_that "сеанс свой, а не того, кто запускал (setsid)" differ "$session" "$my_session"
    assert_that "после второго fork процесс не возглавляет свой сеанс" differ "$session" "$PID"
    assert_that "и группу процессов тоже не возглавляет" differ "$pgrp" "$PID"
    assert_that "родитель — уже не тот, кто запускал" differ "$ppid" "$$"
    assert_that "процесс ждёт событий в состоянии S" same "$state" S
    assert_that "текущий каталог процесса — /" same "$(readlink "/proc/$PID/cwd")" /
    local fd
    for fd in 0 1 2; do
        assert_that "fd $fd ведёт в /dev/null" same "$(readlink "/proc/$PID/fd/$fd")" /dev/null
    done
    assert_that "маска прав umask = 0000" same "$(awk '/^Umask:/ {print $2}' "/proc/$PID/status")" 0000
    local targets
    targets=$(for link in "/proc/$PID"/fd/*; do readlink "$link"; done | sort | tr '\n' ' ')
    assert_that "из унаследованного ничего не осталось: только /dev/null, signalfd и inotify" same "$targets" \
        "/dev/null /dev/null /dev/null anon_inode:[signalfd] anon_inode:inotify "
}

idle_costs_nothing() {
    launch_ok || return
    sleep 1
    local fields
    read -r -a fields <<<"$(stat_fields "$PID")"
    
    local ticks=$((fields[11] + fields[12]))
    assert_that "без событий процессорного времени почти нет ($ticks тиков за секунду с лишним)" test "$ticks" -lt 20
    assert_that "процесс по-прежнему жив" alive "$PID"
}

started_elsewhere() {
    
    mkdir "$WS/launchpad"
    printf 'watch ../a\n' >"$WS/launchpad/diskmond.conf"
    START_DIR="$WS/launchpad"
    launch_ok || return
    assert_that "взят diskmond.conf из каталога запуска" has_line "$WS/stderr.txt" \
        "config $(literal "$WS/launchpad/diskmond.conf"),"
    assert_that "сам демон работает в /" same "$(readlink "/proc/$PID/cwd")" /
    touch "$DIR_A/x"
    expect_line "каталог из этого конфига наблюдается" local0_lines "$(entry_re create "$DIR_A/x")"
}

begin_suite "Демонизация"
scenario goes_to_background "Уход в фон"
scenario no_terminal_own_session "Без терминала, в своём сеансе"
scenario idle_costs_nothing "Ожидание без опроса"
scenario started_elsewhere "Запуск из другого каталога"
end_suite
