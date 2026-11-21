#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 06 знакомство: SIGUSR1 от клиента, ответ хоста с номером, ошибки"

"$BIN/client_sock" >"$WS/usage.txt" 2>&1
assert_that "без аргументов клиент печатает подсказку и выходит с кодом 2" same "$?" 2
assert_that "подсказка содержит формат запуска" has_line "$WS/usage.txt" "<pid хоста>"

"$BIN/client_sock" abc >/dev/null 2>&1
assert_that "нечисловой pid отвергается с кодом 2" same "$?" 2

sleep 0.1 &
dead=$!
wait "$dead"
started=$SECONDS
"$BIN/client_sock" "$dead" Никто >"$WS/dead.txt" 2>&1 </dev/null
code=$?
assert_that "клиент к несуществующему процессу выходит с кодом 1" same "$code" 1
assert_that "и делает это сразу" test $((SECONDS - started)) -le 1
assert_that "с понятным сообщением" has_line "$WS/dead.txt" "не найден"

bash -c 'trap "" USR1; sleep 30' &
deaf=$!
sleep 0.3
started=$SECONDS
"$BIN/client_sock" "$deaf" Ждун >"$WS/deaf.txt" 2>&1 </dev/null
code=$?
spent=$((SECONDS - started))
kill "$deaf" 2>/dev/null
wait "$deaf" 2>/dev/null
assert_that "процесс без ответа на SIGUSR1: клиент выходит с кодом 1" same "$code" 1
assert_that "через 5 с ожидания (прошло ${spent} с)" test "$spent" -ge 4 -a "$spent" -le 7
assert_that "с сообщением о тайм-ауте знакомства" has_line "$WS/deaf.txt" "не ответил на SIGUSR1 за 5 с"

for kind in $KINDS; do
    echo "--- тип связи $kind"
    start_host "$kind" || continue
    for index in 1 2 3 4 5 6 7 8; do
        start_client "$kind" "Гость$index"
    done
    ids=""
    for index in 1 2 3 4 5 6 7 8; do
        if wait_for "$(out_of "Гость$index")" "Ваш номер" 8; then
            ids+="$(grep -o 'Ваш номер [0-9]*' "$(out_of "Гость$index")" | grep -o '[0-9]*$') "
        fi
    done
    unique=$(printf '%s\n' $ids | sort -n | uniq | tr '\n' ' ')
    assert_that "$kind: 8 одновременных клиентов получили разные номера 1..8" same "$unique" "1 2 3 4 5 6 7 8 "
    say Гость1 "/who"
    expect_line "$kind: все 8 видны в списке участников" "$(out_of Гость1)" "8 Гость"
    expect_line "$kind: хост пишет о знакомстве" "$HOST_LOG" "Знакомство с процессом"
    stop_host
    reset_clients
done

summary
