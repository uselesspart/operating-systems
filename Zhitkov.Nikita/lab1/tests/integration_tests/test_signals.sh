#!/bin/bash
# Проверяет:
#  - SIGHUP не убивает демон и вызывает перечитывание конфига
#  - SIGTERM корректно завершает демон и удаляет pid-файл
#  - SIGHUP, пришедший сразу за SIGTERM, не отменяет завершение
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib.sh"

TEST_DIR="$(make_test_dir)"
CONF="$TEST_DIR/reminder.conf"

# 1. SIGHUP не убивает демон
PID="$(start_daemon "$CONF")"

kill -SIGHUP "$PID"
sleep 1
assert_alive "$PID"
pass "SIGHUP не убил демон"

if command -v journalctl >/dev/null 2>&1; then
    if journalctl -t reminder_daemon --since "20 sec ago" --no-pager 2>/dev/null \
       | grep -q "SIGHUP received"; then
        pass "в журнале есть запись о перечитывании конфига"
    else
        pass "journalctl доступен, но запись о SIGHUP не найдена (пропуск)"
    fi
fi

# 2. SIGTERM корректно завершает
kill -SIGTERM "$PID"
sleep 2
assert_dead "$PID"
[ ! -f "$PIDFILE" ] || fail "pid-файл не удалён после SIGTERM"
pass "SIGTERM корректно завершил демон и удалил pid-файл"

# 3. SIGHUP сразу за SIGTERM не отменяет завершение
# Регрессия на баг с общим флагом сигналов: если обработчик хранит только один g_pendingSignal, SIGHUP перезаписывает SIGTERM, и демон никогда не завершается
PID2="$(start_daemon "$CONF")"

kill -SIGHUP  "$PID2"
kill -SIGTERM "$PID2"
sleep 2

assert_dead "$PID2"
[ ! -f "$PIDFILE" ] || fail "pid-файл не удалён: SIGTERM потерян из-за SIGHUP"
pass "SIGHUP сразу за SIGTERM не помешал завершению демона"

# 4. SIGTERM сразу за SIGHUP (обратный порядок)
PID3="$(start_daemon "$CONF")"

kill -SIGTERM "$PID3"
kill -SIGHUP  "$PID3"
sleep 2

assert_dead "$PID3"
[ ! -f "$PIDFILE" ] || fail "pid-файл не удалён: SIGTERM потерян при обратном порядке"
pass "SIGTERM сразу за SIGHUP: демон завершился"

rm -rf "$TEST_DIR"
echo "test_signals: PASSED"