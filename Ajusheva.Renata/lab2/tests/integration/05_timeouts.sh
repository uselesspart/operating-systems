#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 05 тайм-ауты: ожидание семафора не дольше 5 с, затем связь рвётся"

for kind in $KINDS; do
    echo "--- тип связи $kind"

    start_host "$kind" || continue
    start_client "$kind" Ждущий
    wait_for "$(out_of Ждущий)" "Ваш номер" 6
    kill -STOP "$HOST_PID"
    started=$SECONDS
    finish_client Ждущий 12
    spent=$((SECONDS - started))
    kill -CONT "$HOST_PID"
    assert_that "$kind: клиент завершился с кодом 1, когда хост завис" same "${CLIENT_CODE[Ждущий]}" 1
    assert_that "$kind: клиент ждал не дольше 5 с (+запас), прошло ${spent} с" test "$spent" -le 8
    assert_that "$kind: клиент ждал не меньше 4 с" test "$spent" -ge 4
    expect_line "$kind: клиент сообщил о тайм-ауте" "$(out_of Ждущий)" "Связь с хостом потеряна: тайм-аут ожидания"
    expect_line "$kind: хост заметил потерю клиента" "$HOST_LOG" "Клиент 1 «Ждущий» отключён" 8

    start_client "$kind" Замерший
    start_client "$kind" Свидетель
    wait_for "$(out_of Свидетель)" "Ваш номер" 6
    kill -STOP "${CLIENT_PID[Замерший]}"
    expect_line "$kind: хост отключил зависшего клиента через 5 с" "$HOST_LOG" "«Замерший» отключён: не отвечает дольше 5 с" 9
    expect_line "$kind: остальные узнали об отключении" "$(out_of Свидетель)" "Замерший (2) покинул чат: не отвечает дольше 5 с"
    kill -CONT "${CLIENT_PID[Замерший]}"
    finish_client Замерший 8
    assert_that "$kind: размороженный клиент понял, что связи нет (код 1)" same "${CLIENT_CODE[Замерший]}" 1

    kill -KILL "$HOST_PID"
    wait "$HOST_PID" 2>/dev/null
    HOST_PID=""
    started=$SECONDS
    finish_client Свидетель 12
    spent=$((SECONDS - started))
    assert_that "$kind: после SIGKILL хоста клиент завершился с кодом 1" same "${CLIENT_CODE[Свидетель]}" 1
    assert_that "$kind: и сделал это не позже чем через 5 с (+запас), прошло ${spent} с" test "$spent" -le 8
    reset_clients
done

summary
