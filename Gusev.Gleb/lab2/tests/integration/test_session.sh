#!/bin/bash
. "$(dirname "$0")/lib.sh"

suite "Жизненный цикл: fork, потомки, остановка хоста"

case_S01() {
    start_host "$1" --kids 0 --turn-ms 300 || return
    step "козлят нет — ждём 1.5 с"
    sleep 1.5
    expect_eq "ни одного раунда без козлят" "$(round_count)" 0
    expect_true "хост сообщил, что козлят 0" host_log_has "козлят 0"
    expect_true "хост работает" is_running "$HOST_PID"
}
for_each_type S01 "Без козлят ничего не происходит" case_S01

case_S02() {
    start_host "$1" --kids 3 --turn-ms 300 --wolf 50 --seed 1 || return
    wait_until "порождены 3 потомка" 6 child_count_is 3 || return
    wait_until "началась игра с 3 козлятами" 6 host_log_has "Игра началась, козлят: 3" || return
    wait_until "сыграно 3 раунда" 8 rounds_at_least 3 || return
    expect_eq "в раунде участвуют 3 козлёнка" "$(round_lines | head -1 | grep -c 'козлят 3')" 1
    expect_eq "три статуса «присоединился»" "$(count_in "$HOST_LOG" "присоединился")" 3
    expect_true "потомок печатает свой раунд" file_has "$CASE_DIR/host.out" "Раунд 3:"
    expect_true "потомок получает исход раунда" file_has "$CASE_DIR/host.out" "волк выбросил"
}
for_each_type S02 "Три козлёнка: fork, раунды, статусы" case_S02

case_S03() {
    start_host "$1" --kids 5 --turn-ms 500 || return
    wait_until "порождены 5 потомков" 8 child_count_is 5 || return
    wait_until "началась игра с 5 козлятами" 8 host_log_has "Игра началась, козлят: 5" || return
    wait_until "раунд, в котором бросают все 5" 8 host_log_has "козлят 5, порог" || return
    local ids
    ids="$(grep -o 'козлёнок #[0-9]* (pid [0-9]*) присоединился' "$HOST_LOG" \
        | grep -o '#[0-9]*' | sort -u | tr '\n' ' ')"
    expect_eq "номера 1..5 без повторов" "$ids" "#1 #2 #3 #4 #5 "
    local pid
    for pid in $(child_pids); do
        expect_true "потомок $pid зарегистрирован ровно один раз" \
            test "$(count_in "$HOST_LOG" "(pid $pid) присоединился")" -eq 1
    done
}
for_each_type S03 "Пять козлят: разные номера и pid" case_S03

case_S04() {
    start_host "$1" --kids 3 --turn-ms 300 --wolf 50 --seed 2 || return
    wait_until "раунд с тремя козлятами" 8 host_log_has "козлят 3, порог" || return
    local victim
    victim="$(first_child_pid)"
    step "kill -9 потомку $victim"
    kill_quiet KILL "$victim"
    wait_until "хост заметил завершение потомка" 8 host_log_has "(pid $victim) завершился" || return
    wait_until "игра продолжается с двумя козлятами" 6 host_log_has "козлят 2, порог" || return
    expect_eq "живых потомков осталось 2" "$(child_count)" 2
    expect_true "хост работает" is_running "$HOST_PID"
}
for_each_type S04 "Козлёнок убит: хост исключает его, игра продолжается" case_S04

case_S05() {
    start_host "$1" --kids 2 --turn-ms 300 --wolf 50 || return
    wait_until "идёт игра" 8 rounds_at_least 1 || return
    step "SIGTERM хосту"
    kill -TERM "$HOST_PID"
    wait_until "хост завершился" 8 _process_gone "$HOST_PID" || return
    { wait "$HOST_PID"; } 2>/dev/null
    expect_eq "код выхода хоста" "$?" 0
    expect_true "хост записал причину завершения" host_log_has "получен SIGTERM"
    expect_eq "потомков не осталось" "$(child_count)" 0
    expect_true "козлята получили прощание" file_has "$CASE_DIR/host.out" "Волк завершил работу"
    expect_false "каталог /tmp/wolf_<pid> удалён" runtime_dir_exists "$HOST_PID"
}
for_each_type S05 "SIGTERM хосту: BYE потомкам, удаление файлов" case_S05

case_S06() {
    start_host "$1" --kids 2 --turn-ms 300 --wolf 50 || return
    wait_until "идёт игра" 8 rounds_at_least 1 || return
    local kids
    kids="$(child_pids)"
    step "kill -9 хосту — попрощаться он не успеет"
    kill_quiet KILL "$HOST_PID"
    local pid
    for pid in $kids; do
        wait_until "потомок $pid обнаружил обрыв по тайм-ауту" 9 _process_gone "$pid" || return
    done
    expect_true "потомок объяснил причину" file_has "$HOST_LOG" "волк молчит дольше 5 с"
}
for_each_type S06 "Хост убит: потомки выходят по тайм-ауту 5 с" case_S06

case_S07() {
    step "host_$1 --kids 65"
    "$BIN_DIR/host_$1" --kids 65 > "$CASE_DIR/a.out" 2> "$CASE_DIR/a.log"
    expect_eq "код 2: слишком много козлят" "$?" 2
    expect_true "есть справка" file_has "$CASE_DIR/a.log" "Использование"
    step "host_$1 --kids -1"
    "$BIN_DIR/host_$1" --kids -1 > "$CASE_DIR/b.out" 2> "$CASE_DIR/b.log"
    expect_eq "код 2: отрицательное число" "$?" 2
    step "host_$1 --help"
    "$BIN_DIR/host_$1" --help > "$CASE_DIR/c.out" 2> "$CASE_DIR/c.log"
    expect_eq "код 0" "$?" 0
    expect_true "справка в stderr" file_has "$CASE_DIR/c.log" "Использование"
}
for_each_type S07 "Аргументы: границы --kids, --help" case_S07

finish
