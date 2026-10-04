#!/usr/bin/env bash
# Нагрузка: много файлов подряд, ни одно событие не теряется

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

FILES="${DM_TEST_FILES:-500}"

test_many_files()
{
    require_daemon || return
    local i
    for i in $(seq 1 "$FILES"); do
        : >"$D1/f$i"
    done

    local pattern
    pattern="disk_monitor\\[[0-9]+\\]: CREATE +file +$(re "$D1")/f[0-9]+\$"
    local deadline=$(($(date +%s) + 30)) count=0
    while [ "$(date +%s)" -lt "$deadline" ]; do
        count=$(count_lines events "$pattern")
        [ "$count" -ge "$FILES" ] && break
        sleep 0.5
    done
    check "записаны все $FILES событий CREATE (получено $count)" equals "$count" "$FILES"
    expect_no_service "нет переполнения очереди inotify" "queue overflow"
    check "демон работает" is_running "$DAEMON_PID"
}

init_suite "Нагрузка"
run_test test_many_files "Создание $FILES файлов"
finish
