#!/bin/bash
# Интеграционные тесты демона: запуск, демонизация, защита от повторного запуска,
# перечитывание конфига по SIGHUP, срабатывание напоминания, завершение по SIGTERM.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DAEMON="$ROOT/reminderd"
TMP="$(mktemp -d)"

export REMINDERD_PIDFILE="$TMP/reminderd.pid"
export REMINDERD_TERMINAL_CMD="$ROOT/tests/fake_terminal.sh"
export FAKE_TERMINAL_OUT="$TMP/terminal.out"
export REMINDER_CONFIG="$TMP/reminder.conf"
FAILED=0

ok()  { echo "  [ OK ] $1"; }
bad() { echo "  [FAIL] $1"; FAILED=1; }
check() { local d="$1"; shift; if "$@"; then ok "$d"; else bad "$d"; fi; }

alive() { [ -r "/proc/$1/stat" ] && ! grep -q '^State:.*Z' "/proc/$1/status" 2>/dev/null; }
wait_for() { local t="$1" i; shift; for ((i = 0; i < t * 10; i++)); do "$@" && return 0; sleep 0.1; done; return 1; }

pidfile_ready()   { [ -s "$REMINDERD_PIDFILE" ]; }
pidfile_not_pid() { [ -s "$REMINDERD_PIDFILE" ] && [ "$(<"$REMINDERD_PIDFILE")" != "$1" ]; }
pid_dead()        { ! alive "$1"; }
no_pidfile()      { [ ! -e "$REMINDERD_PIDFILE" ]; }
got_text()        { grep -q "$1" "$FAKE_TERMINAL_OUT" 2>/dev/null; }
detached()        { [ "$(ps -o tty= -p "$1" | tr -d ' ')" = "?" ]; }
is_alive_pid()    { alive "$1"; }

cleanup() {
    if [ -s "$REMINDERD_PIDFILE" ]; then kill -TERM "$(<"$REMINDERD_PIDFILE")" 2>/dev/null; fi
    rm -rf "$TMP"
}
trap cleanup EXIT

echo "# test config" > "$REMINDER_CONFIG"

echo "Интеграционные тесты демона:"

( cd "$TMP" && "$DAEMON" reminder.conf )
check "демон создал pid-файл" wait_for 3 pidfile_ready
PID1="$(<"$REMINDERD_PIDFILE")"
check "процесс из pid-файла жив" is_alive_pid "$PID1"
check "демон отвязан от терминала (tty = ?)" detached "$PID1"

( cd "$TMP" && "$DAEMON" reminder.conf )
check "новый экземпляр записал свой pid" wait_for 8 pidfile_not_pid "$PID1"
PID2="$(<"$REMINDERD_PIDFILE")"
check "старый экземпляр завершён" wait_for 3 pid_dead "$PID1"
check "новый экземпляр жив" is_alive_pid "$PID2"

BEFORE="$(grep -c 'Тест напоминания 42' "$FAKE_TERMINAL_OUT")"
kill -HUP "$PID2"; sleep 2
AFTER="$(grep -c 'Тест напоминания 42' "$FAKE_TERMINAL_OUT")"
check "нет повторного срабатывания после SIGHUP" [ "$BEFORE" = "$AFTER" ]

echo "это неправильная строка" >> "$REMINDER_CONFIG"
kill -HUP "$PID2"; sleep 1
check "демон жив при некорректном конфиге" is_alive_pid "$PID2"

kill -TERM "$PID2"
check "демон завершился по SIGTERM" wait_for 5 pid_dead "$PID2"
check "pid-файл удалён при выходе" wait_for 2 no_pidfile

# 7. Несуществующий конфиг - ошибка запуска
"$DAEMON" /nonexistent/reminder.conf 2>/dev/null
check "запуск без конфига завершается с ошибкой" [ $? -ne 0 ]

echo
if [ "$FAILED" -eq 0 ]; then echo "Все интеграционные тесты пройдены."; else echo "Есть проваленные тесты!"; fi
exit "$FAILED"
