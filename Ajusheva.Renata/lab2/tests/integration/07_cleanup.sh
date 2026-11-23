#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 07 ресурсы: каналы и семафоры удаляются, остатки упавшего хоста подчищаются"

sem_count() { ls /dev/shm 2>/dev/null | grep -c "^sem\.lab2chat-$1-" ; }

for kind in $KINDS; do
    echo "--- тип связи $kind"
    start_host "$kind" || continue
    dir="/tmp/lab2chat-$HOST_PID"
    assert_that "$kind: хост создал рабочий каталог" test -d "$dir"
    start_client "$kind" Первый
    wait_for "$(out_of Первый)" "Ваш номер" 6
    assert_that "$kind: на клиента созданы 2 глобальных семафора" same "$(sem_count "$HOST_PID")" 2
    case $kind in
    fifo)
        assert_that "fifo: созданы именованные каналы" test -p "$dir/c1.up.fifo" -a -p "$dir/c1.down.fifo"
        ;;
    pipe)
        assert_that "pipe: клиент открыл два канала хоста через /proc" \
            test "$(ls -l "/proc/${CLIENT_PID[Первый]}/fd" | grep -c 'pipe:')" -ge 2
        assert_that "pipe: хост закрыл свои копии концов клиента" test ! -e "/proc/$HOST_PID/fd/602" -a ! -e "/proc/$HOST_PID/fd/603"
        ;;
    esac
    say Первый "/quit"
    finish_client Первый 6
    sleep 0.5
    assert_that "$kind: после выхода клиента его семафоры удалены" same "$(sem_count "$HOST_PID")" 0
    assert_that "$kind: после выхода клиента его каналы удалены" test -z "$(ls -A "$dir")"

    start_client "$kind" Второй
    wait_for "$(out_of Второй)" "Ваш номер" 6
    old=$HOST_PID
    stop_host
    finish_client Второй 6
    assert_that "$kind: после завершения хоста каталог удалён" test ! -e "/tmp/lab2chat-$old"
    assert_that "$kind: после завершения хоста семафоров нет" same "$(sem_count "$old")" 0

    start_host "$kind" || continue
    start_client "$kind" Третий
    wait_for "$(out_of Третий)" "Ваш номер" 6
    crashed=$HOST_PID
    kill -KILL "$HOST_PID"
    wait "$HOST_PID" 2>/dev/null
    HOST_PID=""
    finish_client Третий 10
    assert_that "$kind: после SIGKILL хоста остались файлы" test -d "/tmp/lab2chat-$crashed"
    start_host "$kind" || continue
    expect_line "$kind: новый хост сообщил об уборке" "$HOST_LOG" "удалены остатки аварийно завершённых хостов"
    assert_that "$kind: каталог упавшего хоста удалён" test ! -e "/tmp/lab2chat-$crashed"
    assert_that "$kind: семафоры упавшего хоста удалены" same "$(sem_count "$crashed")" 0
    stop_host
    reset_clients
done

summary
