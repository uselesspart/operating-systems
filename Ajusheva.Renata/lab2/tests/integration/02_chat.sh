#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 02 чат: общие и личные сообщения, команды, выход клиента и хоста"

for kind in $KINDS; do
    echo "--- тип связи $kind"
    start_host "$kind" || continue
    start_client "$kind" Алиса
    expect_line "$kind: Алиса подключилась под номером 1" "$(out_of Алиса)" "Ваш номер 1, имя «Алиса»"
    start_client "$kind" Борис
    expect_line "$kind: Борис подключился под номером 2" "$(out_of Борис)" "Ваш номер 2, имя «Борис»"
    start_client "$kind" Вера
    expect_line "$kind: Вера подключилась под номером 3" "$(out_of Вера)" "Ваш номер 3, имя «Вера»"
    expect_line "$kind: Алиса узнала о Вере" "$(out_of Алиса)" "Вера (3) вошёл в чат"
    expect_line "$kind: в окне хоста видно подключение" "$HOST_LOG" "Клиент 3 «Вера» присоединился"

    say Алиса "привет всем от $kind"
    for name in Алиса Борис Вера; do
        expect_line "$kind: $name видит общее сообщение" "$(out_of "$name")" "Алиса: привет всем от $kind"
    done
    expect_line "$kind: хост получил общее сообщение" "$HOST_LOG" "от клиента 1 всем"

    say Борис "@1 секрет-$kind"
    expect_line "$kind: Алиса получила личное" "$(out_of Алиса)" "Борис -> вам (лично): секрет-$kind"
    expect_line "$kind: Борис видит своё личное" "$(out_of Борис)" "вы -> Алиса (лично): секрет-$kind"
    say Вера "/to 0 только хосту $kind"
    expect_line "$kind: Вера видит личное хосту" "$(out_of Вера)" "вы -> Хост (лично): только хосту $kind"
    sleep 1
    assert_that "$kind: Вера не видит чужое личное" lacks_line "$(out_of Вера)" "секрет-$kind"
    assert_that "$kind: Алиса не видит личное хосту" lacks_line "$(out_of Алиса)" "только хосту $kind"
    assert_that "$kind: Борис не видит личное хосту" lacks_line "$(out_of Борис)" "только хосту $kind"
    expect_line "$kind: хост отметил пересылку личного" "$HOST_LOG" "от клиента 2 участнику 1"

    say Алиса "@9 никому"
    expect_line "$kind: ошибка для неизвестного адресата" "$(out_of Алиса)" "Сообщение не отправлено: нет участника с номером 9"
    say Алиса "/who"
    expect_line "$kind: /who печатает участников" "$(out_of Алиса)" "участники: 0 Хост, 1 Алиса, 2 Борис, 3 Вера"
    say Алиса "/bad"
    expect_line "$kind: неизвестная команда" "$(out_of Алиса)" "неизвестная команда /bad"

    say Алиса "/quit"
    finish_client Алиса 6
    assert_that "$kind: Алиса вышла с кодом 0" same "${CLIENT_CODE[Алиса]}" 0
    expect_line "$kind: Борис узнал о выходе Алисы" "$(out_of Борис)" "Алиса (1) покинул чат: вышел из чата"
    expect_line "$kind: хост показал отключение" "$HOST_LOG" "Клиент 1 «Алиса» отключён: вышел из чата"

    close_input Вера
    finish_client Вера 6
    assert_that "$kind: конец ввода (Ctrl+D) завершает клиента с кодом 0" same "${CLIENT_CODE[Вера]}" 0

    stop_host
    finish_client Борис 6
    expect_line "$kind: Борис узнал о закрытии чата" "$(out_of Борис)" "Чат закрыт: хост завершает работу"
    assert_that "$kind: Борис завершился с кодом 0" same "${CLIENT_CODE[Борис]}" 0
    reset_clients
done

summary
