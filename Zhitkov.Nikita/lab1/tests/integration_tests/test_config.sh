#!/bin/bash
# Проверяет:
#  - демон принимает конфиг из аргумента командной строки
#  - несуществующий конфиг => ненулевой код возврата
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib.sh"

# Нормальный конфиг из аргумента
TEST_DIR="$(make_test_dir)"
PID="$(start_daemon "$TEST_DIR/reminder.conf")"
assert_alive "$PID"
pass "демон запущен с конфигом $TEST_DIR/reminder.conf"

stop_daemon
rm -rf "$TEST_DIR"

# Несуществующий конфиг => exit code != 0
set +e
"$DAEMON" /tmp/this_config_does_not_exist_$$.conf >/dev/null 2>&1
RC=$?
set -e
[ "$RC" != "0" ] || fail "демон запустился с несуществующим конфигом"
pass "несуществующий конфиг => exit code $RC"

echo "test_config: PASSED"