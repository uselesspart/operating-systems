#!/bin/bash
# Интеграционный тест одного типа связи: bash tests/smoke_test.sh <pipe|fifo|sock>
# Запускает настоящий host_<тип> (Qt, offscreen) — он сам порождает клиентов (fork+exec)
# и проверяет результат по логам. Управление хостом — переменными окружения CHAT_*.
set -u
T="${1:?укажите тип связи: pipe|fifo|sock}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/host_$T"
[ -x "$BIN" ] || { echo "нет $BIN — сначала соберите проект"; exit 1; }

export QT_QPA_PLATFORM=offscreen
fails=0
check() {  # check "описание" "команда"
    if eval "$2"; then echo "  ok   - $1"; else echo "  FAIL - $1"; fails=$((fails+1)); fi
}

# ---------- Сценарий A: 3 клиента, общие и личные сообщения ----------
D="$(mktemp -d)"; cd "$D"
CHAT_AUTOSPAWN=3 CHAT_AUTOSAY=test CHAT_AUTOSAY_TO=2 CHAT_AUTOSAY_MIN=3 CHAT_EXIT_AFTER_MS=7000 \
    "$BIN" >out.txt 2>&1
rc=$?
H="chat_host_$T.log"
check "хост завершился штатно (код 0)" "[ $rc -eq 0 ]"
check "хост запустился" "grep -q 'Хост запущен' out.txt"
for n in 1 2 3; do
    check "клиент $n присоединился (статус на хосте)" "grep -q 'Клиент $n присоединился' $H"
    check "клиент $n получил статусы о подключении" "grep -q 'Подключён к хосту' chat_client${n}_$T.log || grep -q 'подключение к хосту' chat_client${n}_$T.log"
done
for n in 1 2 3; do
    check "хост видит общее сообщение клиента $n" "grep -q 'Клиент $n: test #$n' $H"
    for c in 1 2 3; do
        check "клиент $c видит общее сообщение клиента $n" "grep -q 'Клиент $n: test #$n' chat_client${c}_$T.log"
    done
done
# личное 1 -> 2: видят 1 (эхо) и 2; не видят 3 и хост
check "личное 1->2: видит отправитель"  "grep -q 'PRIVATE test from 1' chat_client1_$T.log"
check "личное 1->2: видит адресат"      "grep -q 'PRIVATE test from 1' chat_client2_$T.log"
check "личное 1->2: не видит клиент 3"  "! grep -q 'PRIVATE test from 1' chat_client3_$T.log"
check "личное 1->2: не видит хост"      "! grep -q 'PRIVATE test from 1' $H"
# личное 3 -> 2
check "личное 3->2: видит отправитель"  "grep -q 'PRIVATE test from 3' chat_client3_$T.log"
check "личное 3->2: видит адресат"      "grep -q 'PRIVATE test from 3' chat_client2_$T.log"
check "личное 3->2: не видит клиент 1"  "! grep -q 'PRIVATE test from 3' chat_client1_$T.log"
check "личное 3->2: не видит хост"      "! grep -q 'PRIVATE test from 3' $H"
sleep 1
check "файлы fifo удалены хостом"       "[ -z \"\$(ls /tmp/chat_fifo_* 2>/dev/null)\" ]"
check "после выхода хоста клиентов не осталось" "! pgrep -f -- '/proc/self/exe --client' >/dev/null"
cd /; rm -rf "$D"

# ---------- Сценарий Б: клиент молчит -> SIGKILL ----------
D="$(mktemp -d)"; cd "$D"
CHAT_AUTOSPAWN=2 CHAT_AUTOSAY=idle CHAT_IDLE_MS=2500 CHAT_EXIT_AFTER_MS=9000 \
    "$BIN" >out.txt 2>&1
rc=$?
H="chat_host_$T.log"
check "хост завершился штатно (код 0)" "[ $rc -eq 0 ]"
check "оба клиента присоединились" "grep -q 'Клиент 1 присоединился' $H && grep -q 'Клиент 2 присоединился' $H"
check "оба клиента убиты за неактивность" "[ \$(grep -c 'молчит более минуты' $H) -ge 2 ]"
check "процессы клиентов завершены сигналом 9 (SIGKILL)" "[ \$(grep -c 'завершён сигналом 9' $H) -ge 2 ]"
check "хост сообщил об отключении клиентов" "grep -q 'Клиент 1 отключился' $H && grep -q 'Клиент 2 отключился' $H"
cd /; rm -rf "$D"

echo "  итого ошибок: $fails"
[ $fails -eq 0 ]
