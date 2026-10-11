#!/bin/bash
# Проверяет защиту от повторного запуска:
#  - устаревший pid-файл (с несуществующим PID) не мешает запуску
#  - повторный запуск вытесняет работающий демон через SIGTERM
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib.sh"

TEST_DIR="$(make_test_dir)"
CONF="$TEST_DIR/reminder.conf"

# Устаревший pid-файл.
echo "999999" > "$PIDFILE"
PID="$(start_daemon "$CONF")"
assert_alive "$PID"
pass "устаревший pid-файл не помешал запуску"

# Повторный запуск вытесняет старый демон
PID1="$PID"
"$DAEMON" "$CONF"
sleep 3
PID2="$(cat "$PIDFILE")"

[ "$PID1" != "$PID2" ] || fail "PID не изменился после перезапуска"
assert_dead "$PID1"
assert_alive "$PID2"
pass "повторный запуск вытеснил старый демон ($PID1 → $PID2)"

rm -rf "$TEST_DIR"
echo "test_pidfile: PASSED"