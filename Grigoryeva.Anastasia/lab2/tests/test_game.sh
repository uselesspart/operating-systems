#!/bin/bash
# Интеграционные тесты: настоящие независимые процессы host_<тип> (консольный режим) и client_<тип>.
# Использование: tests/test_game.sh <каталог_с_исполняемыми_файлами>
# Для каждого типа взаимодействия (pipe, fifo, sock) проверяются: знакомство по сигналам, игровая логика,
# несколько козлят, воскрешение, подключение "на ходу", обрыв связи, корректное завершение и очистка ресурсов.
BIN="$(cd "${1:?укажите каталог с исполняемыми файлами}" && pwd)"
TMP="$(mktemp -d)"
FAILED=0
CHILDREN=()

ok()  { echo "    [ OK ] $1"; }
bad() { echo "    [FAIL] $1"; FAILED=1; }
check() { local d="$1"; shift; if "$@"; then ok "$d"; else bad "$d"; fi; }
has()  { grep -q -- "$2" "$1" 2>/dev/null; }
wait_text() {  # wait_text файл "текст" секунд
    local i; for ((i = 0; i < $3 * 10; i++)); do has "$1" "$2" && return 0; sleep 0.1; done; return 1
}
wait_dead() {  # wait_dead pid секунд
    local i; for ((i = 0; i < $2 * 10; i++)); do kill -0 "$1" 2>/dev/null || return 0; sleep 0.1; done; return 1
}
no_leftovers() {  # no_leftovers pid_хоста
    [ -z "$(ls /dev/shm 2>/dev/null | grep "wolf_$1_")" ] && [ -z "$(ls /tmp 2>/dev/null | grep "wolf_$1_")" ]
}
cleanup() {
    for p in "${CHILDREN[@]}"; do kill -9 "$p" 2>/dev/null; done
    rm -rf "$TMP"
}
trap cleanup EXIT

start_host() {  # start_host тип лог [параметры хоста...] -> HOST_PID
    local t="$1" log="$2"; shift 2
    "$BIN/host_$t" "$@" > "$log" 2>&1 &
    HOST_PID=$!
    CHILDREN+=("$HOST_PID")
    wait_text "$log" "Хост включился" 5
}
start_client() {  # start_client тип лог [параметры клиента...] -> CLIENT_PID
    local t="$1" log="$2"; shift 2
    "$BIN/client_$t" "$HOST_PID" "$@" > "$log" 2>&1 &
    CLIENT_PID=$!
    CHILDREN+=("$CLIENT_PID")
}

for T in pipe fifo sock; do
    echo "=== Тип взаимодействия: $T"
    [ -x "$BIN/host_$T" ] && [ -x "$BIN/client_$T" ] || { bad "нет исполняемых файлов host_$T / client_$T"; continue; }

    # --- 1. Один козленок: ход 1 - попался, ход 2 - остался мертвым -> игра окончена
    echo "  1) один козленок, полная игра"
    L="$TMP/$T.1"; start_host "$T" "$L.h" --wolf 1 --round-delay 50
    start_client "$T" "$L.c" --numbers 100,50
    wait "$CLIENT_PID"; CRC=$?; wait "$HOST_PID"; HRC=$?
    check "знакомство и подключение: козленок присоединился" has "$L.h" "присоединился к игре"
    check "раунд 1: попался, мертвы=1" has "$L.h" "Раунд 1: волк=1, козлят=1, спрятались=0, попались=1, мертвы=1, воскресли=0"
    check "раунд 2: остался мертвым" has "$L.h" "Раунд 2: волк=1, козлят=1, спрятались=0, попались=0, мертвы=1, воскресли=0"
    check "игра окончена после 2 ходов с мертвыми козлятами" has "$L.h" "Игра окончена: все козлята мертвы 2 хода подряд"
    check "хост завершился с кодом 0" [ "$HRC" = 0 ]
    check "клиент получил статус и завершился с кодом 0" [ "$CRC" = 0 ]
    check "клиент написал 'игра окончена'" has "$L.c" "игра окончена, завершаюсь"
    check "ресурсы (семафоры, каналы, сокеты) удалены" no_leftovers "$HOST_PID"

    # --- 2. Три козленка одновременно (n = 3)
    echo "  2) три козленка"
    L="$TMP/$T.2"; start_host "$T" "$L.h" --wolf 1 --wait-kids 3 --round-delay 50
    P=()
    for i in 1 2 3; do start_client "$T" "$L.c$i" --numbers $((110 - i * 10)),$((60 - i * 10)); P+=("$CLIENT_PID"); sleep 0.25; done
    wait "$HOST_PID"; HRC=$?; for p in "${P[@]}"; do wait "$p"; done
    check "игра началась с 3 козлятами" has "$L.h" "Игра началась, козлят: 3"
    check "раунд 1: все три попались" has "$L.h" "Раунд 1: волк=1, козлят=3, спрятались=0, попались=3, мертвы=3, воскресли=0"
    check "в раунде есть строки по каждому козленку" has "$L.h" "козленок #3"
    check "хост завершился с кодом 0" [ "$HRC" = 0 ]
    check "все клиенты получили конец игры" bash -c "[ \$(cat '$L'.c? | grep -c 'игра окончена, завершаюсь') = 3 ]"
    check "ресурсы удалены" no_leftovers "$HOST_PID"

    # --- 3. Воскрешение: живой (100) попадается, мертвый бросает 10 (|10-1| <= 20/n, n=1)
    echo "  3) воскрешение"
    L="$TMP/$T.3"; start_host "$T" "$L.h" --wolf 1 --round-delay 50 --max-rounds 3
    start_client "$T" "$L.c" --numbers 100,10
    wait "$HOST_PID"; wait "$CLIENT_PID"
    check "раунд 2: козленок воскрес" has "$L.h" "Раунд 2: волк=1, козлят=1, спрятались=0, попались=0, мертвы=0, воскресли=1"
    check "игра остановлена по пределу раундов" has "$L.h" "достигнут предел раундов (3)"

    # --- 4. Спрятавшийся козленок и подключение "на ходу"
    echo "  4) подключение козленка во время игры"
    L="$TMP/$T.4"; start_host "$T" "$L.h" --wolf 50 --round-delay 200 --max-rounds 12
    start_client "$T" "$L.c1" --numbers 50,50
    wait_text "$L.h" "Раунд 2: волк=50" 10
    start_client "$T" "$L.c2" --numbers 50,50
    wait "$HOST_PID"; wait
    check "в начале играл 1 козленок, прячется" has "$L.h" "Раунд 1: волк=50, козлят=1, спрятались=1, попались=0"
    check "позже присоединился второй (козлят=2)" has "$L.h" "козлят=2, спрятались=2"
    check "оба клиента получили конец игры" bash -c "[ \$(cat '$L'.c? | grep -c 'игра окончена, завершаюсь') = 2 ]"

    # --- 5. Падение клиента (kill -9): хост замечает обрыв по тайм-ауту/процессу и продолжает жить
    echo "  5) аварийное завершение клиента"
    L="$TMP/$T.5"; export WOLF_IO_TIMEOUT_MS=1000
    start_host "$T" "$L.h" --wolf 50 --round-delay 100
    start_client "$T" "$L.c" --numbers 50,50
    wait_text "$L.c" "статус от волка" 5
    kill -9 "$CLIENT_PID"; wait "$CLIENT_PID" 2>/dev/null
    check "хост заметил обрыв связи" wait_text "$L.h" "связь прервана" 8
    check "хост жив после падения клиента" kill -0 "$HOST_PID"
    kill -TERM "$HOST_PID"; wait "$HOST_PID"; HRC=$?
    check "хост корректно завершается по SIGTERM (код 0)" [ "$HRC" = 0 ]
    check "ресурсы удалены" no_leftovers "$HOST_PID"
    unset WOLF_IO_TIMEOUT_MS

    # --- 6. Остановка хоста по SIGTERM: клиенты получают сообщение об окончании игры
    echo "  6) остановка хоста сигналом"
    L="$TMP/$T.6"
    start_host "$T" "$L.h" --wolf 50 --round-delay 100
    start_client "$T" "$L.c" --numbers 50,50
    wait_text "$L.c" "статус от волка" 5
    kill -TERM "$HOST_PID"; wait "$HOST_PID"; HRC=$?
    wait "$CLIENT_PID"; CRC=$?
    check "хост завершился с кодом 0" [ "$HRC" = 0 ]
    check "клиент уведомлён и завершился с кодом 0" [ "$CRC" = 0 ]
    check "ресурсы удалены" no_leftovers "$HOST_PID"

    # --- 7. Гибель хоста (kill -9): клиент прекращает работу по тайм-ауту
    echo "  7) аварийное завершение хоста"
    L="$TMP/$T.7"; export WOLF_IO_TIMEOUT_MS=1000
    start_host "$T" "$L.h" --wolf 50 --round-delay 100
    start_client "$T" "$L.c" --numbers 50,50
    wait_text "$L.c" "статус от волка" 5
    kill -9 "$HOST_PID"; wait "$HOST_PID" 2>/dev/null
    check "клиент остановился сам по тайм-ауту" wait_dead "$CLIENT_PID" 8
    wait "$CLIENT_PID"; CRC=$?
    check "клиент вышел с кодом 2 (связь потеряна)" [ "$CRC" = 2 ]
    check "клиент сообщил о потере связи" has "$L.c" "связь с хостом потеряна"
    rm -f /dev/shm/sem.wolf_${HOST_PID}_* /tmp/wolf_${HOST_PID}_* 2>/dev/null
    unset WOLF_IO_TIMEOUT_MS
done

# --- 8. Знакомство без ответа: хост "не отвечает" на SIGUSR1 -> клиент останавливается через 5 секунд
echo "=== Общие проверки клиента"
echo "  8) тайм-аут знакомства (5 с)"
sh -c 'trap "" USR1; sleep 30' &
DEAF=$!; CHILDREN+=("$DEAF"); sleep 0.3
T0=$SECONDS
"$BIN/client_pipe" "$DEAF" > "$TMP/deaf.c" 2>&1; CRC=$?
DT=$((SECONDS - T0))
check "клиент завершился с кодом 1" [ "$CRC" = 1 ]
check "ожидание заняло около 5 секунд (было ${DT} с)" [ "$DT" -ge 4 ] && [ "$DT" -le 7 ]
check "сообщение о тайм-ауте знакомства" has "$TMP/deaf.c" "тайм-аут знакомства"

echo "  9) несуществующий хост и неверные аргументы"
"$BIN/client_sock" 999999 > "$TMP/nohost.c" 2>&1; CRC=$?
check "несуществующий pid: код 1 и сообщение" bash -c "[ $CRC = 1 ] && grep -q 'не удалось отправить сигнал хосту' '$TMP/nohost.c'"
"$BIN/client_fifo" > /dev/null 2>&1; check "без аргументов: код 1" [ "$?" = 1 ]
"$BIN/client_fifo" abc > /dev/null 2>&1; check "pid не число: код 1" [ "$?" = 1 ]
"$BIN/host_pipe" --bogus > /dev/null 2>&1; check "хост: неверный параметр -> код 2" [ "$?" = 2 ]

echo
if [ "$FAILED" -eq 0 ]; then echo "Все интеграционные тесты пройдены."; else echo "Есть проваленные тесты!"; fi
exit "$FAILED"
