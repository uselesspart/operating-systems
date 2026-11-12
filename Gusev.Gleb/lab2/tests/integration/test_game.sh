#!/bin/bash
. "$(dirname "$0")/lib.sh"

suite "Игра «Волк и семеро козлят»: правила и согласованность"

case_G01() {
    start_host "$1" --kids 5 --turn-ms 200 --wolf 50 --seed 1 || return
    step "играем до 10 раундов (или до конца игры)"
    _poll 15 rounds_at_least 10 || true
    local report
    report="$(validate_rounds)"
    _log_block "проверка правил" "$report"
    local rounds
    rounds="$(echo "$report" | tail -1 | grep -o 'раундов [0-9]*' | grep -o '[0-9]*')"
    expect_between "сыграно раундов" "${rounds:-0}" 3 1000
    expect_contains "все раунды соответствуют правилам" "$(echo "$report" | tail -1)" "нарушений 0"
    if [ "$(echo "$report" | grep -c НАРУШЕНИЕ)" -gt 0 ]; then
        _fail "нарушения" "$(echo "$report" | grep НАРУШЕНИЕ | head -5)"
    fi
}
for_each_type G01 "Каждый раунд соответствует правилам (5 козлят)" case_G01

case_G02() {
    start_host "$1" --kids 2 --turn-ms 200 --wolf 100 --seed 5 || return
    wait_until "хост объявил конец игры" 30 host_log_has "Игра окончена на раунде" || return
    local last two
    last="$(round_lines | tail -1)"
    two="$(round_lines | tail -2 | grep -c 'мертвы 2$')"
    expect_eq "два последних хода — все мертвы" "$two" 2
    local r_host r_child
    r_host="$(grep -o 'Игра окончена на раунде [0-9]*' "$HOST_LOG" | grep -o '[0-9]*$')"
    r_child="$(grep -o 'Игра окончена на раунде [0-9]*' "$CASE_DIR/host.out" | head -1 | grep -o '[0-9]*$')"
    expect_eq "номер последнего раунда у хоста и потомка совпадает" "$r_child" "$r_host"
    expect_contains "конец игры на последнем сыгранном раунде" "$last" "раунд $r_host:"
    expect_contains "правила соблюдены" "$(validate_rounds | tail -1)" "нарушений 0"
    expect_true "хост продолжает работать после конца игры" is_running "$HOST_PID"
}
for_each_type G02 "Конец игры: все мертвы 2 хода подряд" case_G02

case_G03() {
    start_host "$1" --kids 2 --turn-ms 300 --seed 3 || return
    wait_until "сыграно 3 раунда" 10 rounds_at_least 3 || return
    local id
    for id in 1 2; do
        local line host_throw kid_throw host_wolf kid_wolf
        line="$(round_lines | grep "раунд 1:")"
        host_throw="$(echo "$line" | grep -o "#$id [0-9]*" | awk '{print $2}')"
        host_wolf="$(echo "$line" | grep -o 'волк [0-9]*' | awk '{print $2}')"
        kid_throw="$(grep "kid_$1#$id INFO : бросок в раунде 1" "$HOST_LOG" | grep -o 'бросаю [0-9]*' | awk '{print $2}')"
        kid_wolf="$(grep "kid_$1#$id INFO : итог раунда 1" "$HOST_LOG" | grep -o 'волк [0-9]*' | awk '{print $2}')"
        expect_eq "раунд 1, козлёнок #$id: бросок у хоста = у потомка" "$host_throw" "$kid_throw"
        expect_eq "раунд 1, козлёнок #$id: число волка у хоста = у потомка" "$host_wolf" "$kid_wolf"
    done
}
for_each_type G03 "Хост и потомки видят одни и те же числа" case_G03

case_G04() {
    start_host "$1" --kids 1 || return
    wait_until "сыграно 2 раунда" 15 rounds_at_least 2 || return
    local t1 t2 ms
    t1="$(round_lines | sed -n 1p | grep -o '^\[[0-9:.]*\]' | tr -d '[]')"
    t2="$(round_lines | sed -n 2p | grep -o '^\[[0-9:.]*\]' | tr -d '[]')"
    ms="$(awk -v a="$t1" -v b="$t2" 'BEGIN { split(a, x, ":"); split(b, y, ":");
        print int(((y[1]*3600 + y[2]*60 + y[3]) - (x[1]*3600 + x[2]*60 + x[3])) * 1000) }')"
    expect_between "между раундами ≈ 3 с ожидания ввода волка (мс)" "$ms" 2900 5500
    expect_true "без ввода число волка выбрано случайно" host_log_has "(случайно)"
}
for_each_type G04 "Волк ждёт ввод 3 с, затем бросает случайно" case_G04

finish
