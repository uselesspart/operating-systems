#!/usr/bin/env bash
# Сбои: неверные аргументы, нет хоста, тайм-ауты 5 с, SIGKILL молчащим клиентам,
# аварийное завершение хоста и клиента, уборка за упавшим хостом

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

finished_pid()
{
    sleep 0 &
    local pid=$!
    wait "$pid"
    echo "$pid"
}

test_usage()
{
    start_client без_аргументов --help
    expect_exit "без pid хоста: код 2" без_аргументов 2
    expect_output "подсказка по запуску" без_аргументов "Использование:"
    start_client лишние 1 2 3
    expect_exit "лишние аргументы: код 2" лишние 2
}

test_no_such_host()
{
    local started
    started=$(now_ms)
    start_client алиса "$(finished_pid)" алиса
    expect_exit "нет процесса: клиент завершается с кодом 1" алиса 1 2
    check "сразу, без ожидания" between $(($(now_ms) - started)) 0 2000
    expect_output "сообщение об ошибке" алиса "не найден или недоступен"
}

test_host_does_not_answer()
{
    spawn silent bash -c 'trap "" USR1; exec sleep 30'
    local silent=$SPAWNED_PID started
    HOST_PIDS+=("$silent")
    sleep 0.2
    started=$(now_ms)
    start_client алиса "$silent" алиса
    expect_exit "нет ответа на SIGUSR1: клиент завершается с кодом 1" алиса 1 10
    check "через 5 с" between $(($(now_ms) - started)) 4800 7000
    expect_output "сообщение о тайм-ауте" алиса "не ответил на SIGUSR1 за 5 с"
}

test_silent_client_is_killed()
{
    HOST_ENV=(CHAT_IDLE_LIMIT_SEC=2)
    require_host || return
    join_chat молчун && join_chat болтун || return
    local i
    for i in 1 2 3 4 5 6 7 8; do
        say болтун "сообщение $i"
        sleep 0.4
    done
    expect_exit "молчащий клиент убит SIGKILL" молчун 137
    check "пишущий клиент продолжает работать" is_running "${CLIENT_PID[болтун]}"
    expect_host_log "хост записал SIGKILL" "Client 1 idle for more than 2 s: SIGKILL to pid"
    expect_output "остальные видят причину" болтун "* молчун покинул чат (не писал дольше 2 с, отправлен SIGKILL)"
}

test_client_killed()
{
    require_host || return
    join_chat алиса && join_chat боб || return
    kill -KILL "${CLIENT_PID[алиса]}"
    expect_host_log "хост замечает пропажу клиента за 5 с" "Client 1 disconnected: не отвечает дольше 5 с" 8
    expect_output "остальные видят уход" боб "* алиса покинул чат (не отвечает дольше 5 с)"
    say боб "я остался"
    expect_output "чат продолжает работать" боб "] Вы: я остался"
}

test_host_killed()
{
    require_host || return
    join_chat алиса || return
    local started
    started=$(now_ms)
    kill -KILL "$HOST_PID"
    expect_exit "клиент завершается с кодом 1" алиса 1 8
    check "не позже чем через 5 с ожидания" between $(($(now_ms) - started)) 0 7000
    expect_output "клиенту сообщили о потере связи" алиса "* Связь с хостом потеряна"
}

test_stale_files_are_removed()
{
    require_host || return
    join_chat алиса || return
    local dead=$HOST_PID dead_name=$HOST_NAME
    kill -KILL "$dead"
    wait_status "$dead_name"
    check "после SIGKILL остались каталог и семафоры" not equals "$(leftovers_of "$dead")" ""

    require_host || return
    expect_host_log "новый хост убрал старый каталог" "Removed stale runtime directory /tmp/chat_$dead"
    check "семафоры упавшего хоста удалены" equals "$(leftovers_of "$dead")" ""
}

test_two_hosts()
{
    require_host || return
    local first=$HOST_PID
    join_chat алиса || return
    require_host || return
    join_chat боб || return
    expect_output "второй хост выдаёт свои номера" боб "вы участник 1 «боб»"
    say алиса "только первому хосту"
    expect_no_output "хосты не мешают друг другу" боб "только первому хосту"
    check "первый хост работает" is_running "$first"
}

failure_suite()
{
    run_test test_usage "Неверные аргументы клиента"
    run_test test_no_such_host "Хоста с таким pid нет"
    run_test test_host_does_not_answer "Хост не отвечает на рукопожатие"
    run_test test_silent_client_is_killed "Молчащий клиент"
    run_test test_client_killed "Клиент убит SIGKILL"
    run_test test_host_killed "Хост убит SIGKILL"
    run_test test_stale_files_are_removed "Уборка за упавшим хостом"
    run_test test_two_hosts "Два хоста одновременно"
}

init_suite "Сбои и тайм-ауты"
run_for_each_type failure_suite
finish
