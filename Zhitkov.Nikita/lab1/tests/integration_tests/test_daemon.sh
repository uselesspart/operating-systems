#!/bin/bash
# Проверяет, что демон действительно отвязан от терминала:
#  - жив после запуска
#  - родитель — не наш shell (PPID = 1 или systemd --user)
#  - нет управляющего терминала (TTY = ?)
#  - рабочий каталог = /
#  - stdin/stdout/stderr => /dev/null
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$HERE/lib.sh"

TEST_DIR="$(make_test_dir)"
PID="$(start_daemon "$TEST_DIR/reminder.conf")"

assert_alive "$PID"

# PPID: на современных системах 1 (systemd) или systemd --user
# Главное, чтобы это был НЕ наш shell
PPID_VAL=$(ps -o ppid= -p "$PID" | tr -d ' ')
[ "$PPID_VAL" != "$$" ] || fail "PPID=$PPID_VAL — демон не отвязан от родителя"
pass "PPID=$PPID_VAL (не наш shell)"

TTY=$(ps -o tty= -p "$PID" | tr -d ' ')
[ "$TTY" = "?" ] || fail "у демона есть управляющий терминал: $TTY"
pass "управляющий терминал отсутствует (TTY=?)"

CWD=$(readlink "/proc/$PID/cwd")
[ "$CWD" = "/" ] || fail "рабочий каталог демона: $CWD, ожидается /"
pass "рабочий каталог = /"

for fd in 0 1 2; do
    TARGET=$(readlink "/proc/$PID/fd/$fd")
    [ "$TARGET" = "/dev/null" ] || fail "fd$fd указывает на $TARGET, ожидается /dev/null"
done
pass "stdin/stdout/stderr => /dev/null"

rm -rf "$TEST_DIR"
echo "test_daemon: PASSED"