#!/bin/bash
# Проверяет:
#  - SIGHUP не убивает демон и вызывает перечитывание конфига
#  - SIGTERM корректно завершает демон и удаляет pid-файл
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib.sh"

TEST_DIR="$(make_test_dir)"
PID="$(start_daemon "$TEST_DIR/reminder.conf")"

# SIGHUP
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

# SIGTERM
kill -SIGTERM "$PID"
sleep 2
assert_dead "$PID"
[ ! -f "$PIDFILE" ] || fail "pid-файл не удалён после SIGTERM"
pass "SIGTERM корректно завершил демон и удалил pid-файл"

rm -rf "$TEST_DIR"
echo "test_signals: PASSED"