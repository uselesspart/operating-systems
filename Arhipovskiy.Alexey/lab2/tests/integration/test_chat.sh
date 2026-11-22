#!/usr/bin/env bash
# Чат: подключение, общие и личные сообщения, порядок, выход клиента, завершение хоста

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

test_host_starts()
{
    require_host || return
    expect_host_log "хост пишет pid и тип связи" "chat_host[$HOST_PID]: Host started: pid $HOST_PID, connection type $TYPE"
    check "создан каталог /tmp/chat_$HOST_PID" test -d "/tmp/chat_$HOST_PID"
    check "хост работает" is_running "$HOST_PID"
}

test_clients_join()
{
    require_host || return
    join_chat алиса || return
    join_chat боб || return
    expect_output "алисе выдан номер 1" алиса "вы участник 1 «алиса»"
    expect_output "бобу выдан номер 2" боб "вы участник 2 «боб»"
    expect_host_log "рукопожатие по SIGUSR1" "Handshake from pid ${CLIENT_PID[алиса]}: client id 1"
    expect_host_log "хост видит подключение" "Client 2 (боб, pid ${CLIENT_PID[боб]}) joined"
    expect_output "алиса видит, что боб присоединился" алиса "* боб присоединился к чату"
}

test_same_names()
{
    require_host || return
    join_chat гость || return
    start_client гость2 "$HOST_PID" гость
    expect_output "одинаковое имя получает номер" гость2 "вы участник 2 «гость (2)»"
}

test_public_message()
{
    require_host || return
    join_chat алиса && join_chat боб || return
    say алиса "привет всем"
    expect_output "боб получил общее сообщение" боб "] алиса: привет всем"
    expect_output "алиса видит своё сообщение" алиса "] Вы: привет всем"
    expect_host_log "хост переслал сообщение" "from client 1 to everyone"
}

test_private_message()
{
    require_host || return
    join_chat алиса && join_chat боб && join_chat кэрол || return
    say боб "@1 секрет"
    expect_output "получатель видит личное сообщение" алиса "] боб → вам (лично): секрет"
    expect_output "отправитель видит своё личное сообщение" боб "] Вы → алиса (лично): секрет"
    expect_host_log "хост переслал личное сообщение" "from client 2 to client 1"
    expect_no_output "третий участник его не видит" кэрол "секрет"
}

test_broken_input()
{
    require_host || return
    join_chat алиса && join_chat боб || return
    say алиса $'при\xd0вет\e[D\e[D'
    expect_output "сообщение дошло" боб "алиса: привет"
    check "без половинок букв и кодов стрелок" grep -q -- '] алиса: привет$' "$(output_of боб)"
}

test_message_to_host_and_errors()
{
    require_host || return
    join_chat алиса || return
    say алиса "/to 0 хосту лично"
    expect_host_log "личное сообщение хосту" "from client 1 to host"
    say алиса "@7 никому"
    expect_output "неизвестный получатель" алиса "* Участник 7 не найден"
    say алиса "@1 себе"
    expect_output "самому себе нельзя" алиса "* Нельзя отправить личное сообщение самому себе"
    say алиса "/kick 2"
    expect_output "неизвестная команда" алиса "! Неизвестная команда"
}

test_who()
{
    require_host || return
    join_chat алиса && join_chat боб || return
    say боб "/who"
    expect_output "список участников" боб "* Участники: 0 Хост; 1 алиса; 2 боб (вы);"
}

test_send_order()
{
    require_host || return
    join_chat алиса && join_chat боб && join_chat кэрол || return
    local i
    for i in 1 2 3 4; do
        say алиса "a$i"
        sleep 0.1
        say боб "b$i"
        sleep 0.1
    done
    expect_output "все сообщения дошли" кэрол "боб: b4"
    local order
    order=$(grep -oE '(алиса|боб): [ab][0-9]' "$(output_of кэрол)" | sed 's/.*: //' | tr '\n' ' ')
    check "сообщения от разных клиентов идут в порядке отправки" equals "$order" "a1 b1 a2 b2 a3 b3 a4 b4 "
}

test_client_quits()
{
    require_host || return
    join_chat алиса && join_chat боб || return
    say алиса "/quit"
    expect_exit "клиент вышел с кодом 0" алиса 0
    expect_output "клиенту сообщили о выходе" алиса "* Чат закрыт: вы вышли из чата"
    expect_output "остальные видят уход" боб "* алиса покинул чат (вышел)"
    expect_host_log "хост видит отключение" "Client 1 disconnected: вышел"
}

test_end_of_input()
{
    require_host || return
    join_chat алиса || return
    close_input алиса
    expect_exit "конец ввода: клиент выходит из чата" алиса 0
    expect_host_log "хост видит отключение" "Client 1 disconnected: вышел"
}

test_client_sigint()
{
    require_host || return
    join_chat алиса || return
    kill -INT "${CLIENT_PID[алиса]}"
    expect_exit "SIGINT: клиент корректно выходит" алиса 0
    expect_output "клиент попрощался с хостом" алиса "* Чат закрыт: вы вышли из чата"
}

test_host_terminates()
{
    require_host || return
    join_chat алиса && join_chat боб || return
    local host=$HOST_PID
    kill -TERM "$host"
    expect_exit "клиент завершился с кодом 0" алиса 0
    expect_output "клиенту сообщили о завершении хоста" алиса "* Чат закрыт: хост завершает работу"
    expect_exit "второй клиент тоже" боб 0
    expect_exit "хост завершился с кодом 0" "$HOST_NAME" 0
    expect_host_log "хост записал остановку" "Host stopped"
    check "каталог и семафоры хоста удалены" equals "$(leftovers_of "$host")" ""
}

chat_suite()
{
    run_test test_host_starts "Запуск хоста"
    run_test test_clients_join "Подключение клиентов"
    run_test test_same_names "Одинаковые имена"
    run_test test_public_message "Общее сообщение"
    run_test test_private_message "Личное сообщение"
    run_test test_broken_input "Испорченный ввод"
    run_test test_message_to_host_and_errors "Сообщение хосту и ошибки"
    run_test test_who "Список участников"
    run_test test_send_order "Порядок сообщений"
    run_test test_client_quits "Выход клиента по /quit"
    run_test test_end_of_input "Выход клиента по концу ввода"
    run_test test_client_sigint "Выход клиента по SIGINT"
    run_test test_host_terminates "Завершение хоста по SIGTERM"
}

init_suite "Чат"
run_for_each_type chat_suite
finish
