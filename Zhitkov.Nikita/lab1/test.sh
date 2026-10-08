#!/bin/bash
# Полный набор тестов: сборка, unit-тесты, интеграционные тесты демона.
set -e

ROOT="$(cd "$(dirname "$0")" && pwd)"

echo "=== Building ==="
"$ROOT/build.sh"

echo "=== Unit tests ==="
"$ROOT/tests/run_unit_tests.sh"

echo "=== Integration test ==="

TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR" /tmp/reminder_daemon.pid' EXIT

rm -f /tmp/reminder_daemon.pid

cat > "$TEST_DIR/config.ini" <<EOF
interval = 1
events_file = $TEST_DIR/events.txt
EOF

touch "$TEST_DIR/events.txt"

# 1. Запуск демона.
"$ROOT/reminder_daemon" "$TEST_DIR/config.ini"
sleep 1

if [ ! -f /tmp/reminder_daemon.pid ]; then
    echo "FAIL: pid-файл не создан"
    exit 1
fi

PID1="$(cat /tmp/reminder_daemon.pid)"
if ! kill -0 "$PID1" 2>/dev/null; then
    echo "FAIL: демон не работает"
    exit 1
fi
echo "OK: демон запущен, pid=$PID1"

# 2. Проверяем защиту от повторного запуска.
"$ROOT/reminder_daemon" "$TEST_DIR/config.ini"
sleep 3

PID2="$(cat /tmp/reminder_daemon.pid)"
if [ "$PID1" = "$PID2" ]; then
    echo "FAIL: pid не изменился после повторного запуска"
    exit 1
fi
if kill -0 "$PID1" 2>/dev/null; then
    echo "FAIL: старый демон всё ещё работает"
    exit 1
fi
if ! kill -0 "$PID2" 2>/dev/null; then
    echo "FAIL: новый демон не запущен"
    exit 1
fi
echo "OK: старый демон вытеснен новым (pid=$PID2)"

# 3. SIGHUP — перезагрузка конфига.
kill -HUP "$PID2"
sleep 1
if ! kill -0 "$PID2" 2>/dev/null; then
    echo "FAIL: демон умер после SIGHUP"
    exit 1
fi
echo "OK: SIGHUP обработан"

# 4. Добавляем событие и ждём срабатывания (openTerminal не сработает
#    без DISPLAY, но проверяем, что процесс не падает).
NOW="$(date '+%d.%m.%Y %H:%M:%S')"
echo "$NOW Test event text" >> "$TEST_DIR/events.txt"
sleep 3
if ! kill -0 "$PID2" 2>/dev/null; then
    echo "FAIL: демон умер после обработки события"
    exit 1
fi
echo "OK: событие обработано"

# 5. SIGTERM — корректное завершение.
kill -TERM "$PID2"
sleep 2
if kill -0 "$PID2" 2>/dev/null; then
    echo "FAIL: демон не завершился по SIGTERM"
    exit 1
fi
if [ -f /tmp/reminder_daemon.pid ]; then
    echo "FAIL: pid-файл не удалён"
    exit 1
fi
echo "OK: демон корректно завершён по SIGTERM"

echo
echo "ALL TESTS PASSED"