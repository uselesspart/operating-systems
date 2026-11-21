#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 04 молчание: клиент, который не пишет дольше предела, получает SIGKILL"

for kind in $KINDS; do
    echo "--- тип связи $kind"
    start_host "$kind" LAB2_SILENCE_SEC=3 || continue
    start_client "$kind" Тихоня
    start_client "$kind" Болтун
    wait_for "$(out_of Болтун)" "Ваш номер 2" 6
    for tick in 1 2 3 4 5 6; do
        say Болтун "тик $tick"
        sleep 1
    done
    pid=${CLIENT_PID[Тихоня]}
    assert_that "$kind: молчащий клиент завершён" gone "$pid"
    finish_client Тихоня 1
    assert_that "$kind: молчащий клиент убит SIGKILL (код 137)" same "${CLIENT_CODE[Тихоня]}" 137
    assert_that "$kind: пишущий клиент продолжает работу" alive "${CLIENT_PID[Болтун]}"
    expect_line "$kind: остальные узнали причину" "$(out_of Болтун)" "Тихоня (1) покинул чат: молчал дольше 3 с, отправлен SIGKILL"
    expect_line "$kind: хост записал SIGKILL в журнал" "$HOST_LOG" "молчит слишком долго: отправлен SIGKILL"
    expect_line "$kind: хост показал отключение" "$HOST_LOG" "Клиент 1 «Тихоня» отключён: молчал дольше 3 с"
    stop_host
    reset_clients
done

summary
