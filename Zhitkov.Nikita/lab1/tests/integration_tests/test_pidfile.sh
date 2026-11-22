#!/bin/bash
# Проверяет защиту от повторного запуска и доверие к pid-файлу:
#  - устаревший pid-файл (несуществующий PID) не мешает запуску
#  - повторный запуск вытесняет работающий демон через SIGTERM
#  - чужой PID в pid-файле (не reminder_daemon) не убивается
#  - pid-файл создаётся с правами 0644, а не 0666
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib.sh"

TEST_DIR="$(make_test_dir)"
CONF="$TEST_DIR/reminder.conf"

# 1. Устаревший pid-файл не блокирует запуск
# Запускаем вручную, а не через start_daemon: start_daemon стирает pid-файл
rm -f "$PIDFILE"
echo "999999" > "$PIDFILE"

"$DAEMON" "$CONF"
sleep 1

PID1="$(cat "$PIDFILE" 2>/dev/null || echo "")"
[ -n "$PID1" ]          || fail "демон не записал свой PID в pid-файл"
[ "$PID1" != "999999" ] || fail "демон не перезаписал устаревший pid-файл"
assert_alive "$PID1"
pass "устаревший pid-файл не помешал запуску (стало $PID1)"

# 2. Повторный запуск вытесняет старый демон
"$DAEMON" "$CONF"
sleep 3

PID2="$(cat "$PIDFILE")"
[ "$PID1" != "$PID2" ] || fail "PID не изменился после перезапуска"
assert_dead "$PID1"
assert_alive "$PID2"
pass "повторный запуск вытеснил старый демон ($PID1 -> $PID2)"

# Останавливаем текущий демон, чтобы не мешал следующим блокам
kill -TERM "$PID2" 2>/dev/null || true
sleep 1
rm -f "$PIDFILE"

# 3. Чужой PID в pid-файле не убивается
# Регрессия: раньше демон читал pid-файл с правами 0666 и убивал любой PID без проверки. Теперь сверяется /proc/<pid>/comm == "reminder_daemon"
sleep 30 &
FOREIGN_PID=$!
echo "$FOREIGN_PID" > "$PIDFILE"

"$DAEMON" "$CONF"
sleep 1

PID3="$(cat "$PIDFILE" 2>/dev/null || echo "")"
[ -n "$PID3" ]                || fail "демон не запустился при чужом PID в pid-файле"
[ "$PID3" != "$FOREIGN_PID" ] || fail "демон не перезаписал чужой PID"

# kill -0 возвращает 0 и для зомби, поэтому проверяем состояние явно
STAT="$(ps -o stat= -p "$FOREIGN_PID" 2>/dev/null | tr -d ' ')"
case "$STAT" in
    "")  fail "чужой процесс $FOREIGN_PID исчез — демон его убил" ;;
    Z*)  fail "чужой процесс $FOREIGN_PID стал зомби — демон отправил ему сигнал" ;;
esac
pass "чужой PID ($FOREIGN_PID) в pid-файле проигнорирован, не убит"

kill -TERM "$PID3" 2>/dev/null || true
kill "$FOREIGN_PID" 2>/dev/null || true
sleep 1
rm -f "$PIDFILE"

# 4. Права pid-файла 0644, а не 0666
# Регрессия: старый writePidFile через std::ofstream при umask(0) создавал файл с правами 0666 — его мог перезаписать любой пользователь
"$DAEMON" "$CONF"
sleep 1

[ -f "$PIDFILE" ] || fail "демон не создал pid-файл"

MODE="$(stat -c '%a' "$PIDFILE")"
[ "$MODE" = "644" ] || fail "pid-файл имеет права $MODE, ожидается 644"
pass "pid-файл создан с правами 0644"

kill -TERM "$(cat "$PIDFILE")" 2>/dev/null || true
sleep 1

rm -rf "$TEST_DIR"
echo "test_pidfile: PASSED"