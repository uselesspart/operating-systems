#!/usr/bin/env bash
# shellcheck disable=SC2119 # launch_ok вызывается и с аргументами, и без
# Сигналы: SIGHUP перечитывает конфиг, SIGTERM — сообщение о выходе в системный журнал и завершение

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"

hup_adds_directory() {
    local third="$WS/c"
    mkdir "$third"
    launch_ok || return
    touch "$third/early"
    expect_silence "пока конфиг не перечитан, третий каталог не наблюдается" local0_lines "$(literal "$third")"

    echo "watch $third" >>"$CONF"
    kill -HUP "$PID"
    expect_line "SIGHUP: перечитан тот же конфиг по запомненному абсолютному пути" daemon_lines \
        "^daemon\\.info diskmond\\[$PID\\]: SIGHUP from pid [0-9]+: rereading $(literal "$CONF")\$"
    expect_line "SIGHUP: итог перечитывания" daemon_lines "reload done, 3 directories under 3 roots\$"
    expect_line "SIGHUP: отметка <reload> в журнале событий" local0_lines "<reload> 3 directories under 3 roots\$"
    touch "$third/late"
    expect_line "после SIGHUP третий каталог наблюдается" local0_lines "$(entry_re create "$third/late")"
    assert_that "процесс тот же — перезапуска не было" same "$(cat "$PIDF")" "$PID"
}

hup_drops_directory() {
    launch_ok || return
    printf 'watch %s\n' "$DIR_A" >"$CONF"
    kill -HUP "$PID"
    expect_line "каталог, убранный из конфига, отпускается" daemon_lines "no longer watching $(literal "$DIR_B")\$"
    remember_position
    touch "$DIR_B/gone" "$DIR_A/kept"
    expect_line "оставшийся каталог наблюдается" local0_lines "$(entry_re create "$DIR_A/kept")"
    expect_silence "убранный — нет" local0_lines "$(literal "$DIR_B")"
}

hup_without_gaps() {
    mkdir -p "$DIR_A/sub"
    launch_ok || return
    kill -HUP "$PID"
    touch "$DIR_A/sub/meanwhile"
    expect_line "событие рядом с перечитыванием не потерялось" local0_lines "$(entry_re create "$DIR_A/sub/meanwhile")"
    expect_line "перечитывание прошло" daemon_lines "reload done"
    assert_that "неизменившиеся каталоги не обходились заново" \
        same "$(tally daemon_lines "watching $(literal "$DIR_A") ")" 1
}

hup_when_config_vanished() {
    launch_ok || return
    mv "$CONF" "$CONF.away"
    kill -HUP "$PID"
    expect_line "ошибка чтения конфига записана" daemon_lines \
        "^daemon\\.err .*config $(literal "$CONF") cannot be opened: No such file or directory\$"
    expect_line "прежние настройки остаются в силе" daemon_lines "reload aborted, the previous settings stay in effect\$"
    touch "$DIR_A/still"
    expect_line "наблюдение продолжается" local0_lines "$(entry_re create "$DIR_A/still")"
    assert_that "демон жив" alive "$PID"

    mv "$CONF.away" "$CONF"
    kill -HUP "$PID"
    expect_line "когда конфиг вернули, SIGHUP снова работает" daemon_lines "reload done"
}

hup_burst() {
    launch_ok || return
    local _
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        kill -HUP "$PID"
    done
    expect_line "серия из десяти SIGHUP обработана" daemon_lines "reload done"
    assert_that "демон её пережил" alive "$PID"
    remember_position
    touch "$DIR_A/after"
    expect_line "и продолжает записывать события" local0_lines "$(entry_re create "$DIR_A/after")"
}

term_stops_daemon() {
    launch_ok || return
    touch "$DIR_A/last"
    kill -TERM "$PID"
    expect_line "SIGTERM: сообщение о выходе в системном журнале" daemon_lines \
        "^daemon\\.info diskmond\\[$PID\\]: SIGTERM from pid [0-9]+: exiting\$"
    expect_line "SIGTERM: завершение подтверждено" daemon_lines "^daemon\\.notice diskmond\\[$PID\\]: shutdown complete\$"
    expect_line "журнал событий закрыт отметкой <stop>" local0_lines "^local0\\.info diskmond\\[$PID\\]: <stop>\$"
    assert_that "событие, случившееся до сигнала, записано" has_line "$CAPTURE" "$(entry_re create "$DIR_A/last")"
    assert_that "процесс завершился" await_exit "$PID"
    assert_that "свой pid-файл демон убрал" test ! -e "$PIDF"
    assert_that "shutdown complete — последнее, что демон записал" \
        has_line <(tail -n 1 "$CAPTURE") "shutdown complete\$"
}

begin_suite "Сигналы"
scenario hup_adds_directory "SIGHUP: каталог добавили"
scenario hup_drops_directory "SIGHUP: каталог убрали"
scenario hup_without_gaps "SIGHUP: без перерыва в наблюдении"
scenario hup_when_config_vanished "SIGHUP: конфиг пропал"
scenario hup_burst "Много SIGHUP подряд"
scenario term_stops_daemon "SIGTERM"
end_suite
