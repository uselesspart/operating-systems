#!/usr/bin/env bash
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$DIR/lab1_daemon"
PIDFILE=/tmp/lab1_daemon.pid
LOGFILE="${LAB1_LOGFILE:-/var/log/lab1_disk_monitor.log}"
WORK="$(mktemp -d)"
RUN="t$$"
PASS=0
FAIL=0
SKIP=0

ok()   { echo "  [PASS] $1"; PASS=$((PASS + 1)); }
bad()  { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }
skip() { echo "  [SKIP] $1"; SKIP=$((SKIP + 1)); }
check() { local d="$1"; shift; if "$@"; then ok "$d"; else bad "$d"; fi; }

alive() { [ -r "/proc/$1/stat" ] && ! grep -q ') Z' "/proc/$1/stat"; }
dead()  { ! alive "$1"; }
wait_for() { local t=$1; shift; for ((i = 0; i < t * 10; i++)); do "$@" && return 0; sleep 0.1; done; return 1; }

pidfile_alive() { [ -s "$PIDFILE" ] && alive "$(cat "$PIDFILE")"; }
pidfile_gone()  { [ ! -e "$PIDFILE" ]; }

read_log() {
    [ -r "$LOGFILE" ] && cat "$LOGFILE"
    command -v journalctl >/dev/null 2>&1 && journalctl -t lab1_daemon -n 5000 --no-pager 2>/dev/null
    return 0
}
log_has()    { read_log | grep -qF -- "$1"; }
log_hasnot() { ! log_has "$1"; }
logged()     { wait_for 5 log_has "$1"; }

cleanup() {
    [ -s "$PIDFILE" ] && kill -TERM "$(cat "$PIDFILE")" 2>/dev/null
    sleep 0.3
    rm -rf "$WORK" "$PIDFILE"
}
trap cleanup EXIT

echo "== Сборка"
build_quiet() { bash "$DIR/build.sh" >/dev/null; }
check "build.sh завершается успешно" build_quiet
check "исполняемый файл создан" test -x "$BIN"
check "после сборки нет лишних *.o файлов" test -z "$(find "$DIR" -name '*.o')"

echo "== Некорректный запуск"
( cd "$WORK" && "$BIN" no_such.conf ) 2>/dev/null
check "ненулевой код возврата при отсутствии конфига" test $? -ne 0
printf 'folder = /var/log\n' > "$WORK/bad.conf"
( cd "$WORK" && "$BIN" bad.conf ) 2>/dev/null
check "слежение за /var/log запрещено (защита от цикла)" test $? -ne 0
check "демон не запустился при ошибках" test ! -e "$PIDFILE"

echo "== Запуск демона (конфиг указан относительным путём)"
mkdir -p "$WORK/a" "$WORK/b" "$WORK/c"
printf 'folder = a\nfolder = b\nrecursive = yes\n' > "$WORK/test.conf"
( cd "$WORK" && "$BIN" test.conf )
check "pid-файл создан и процесс жив" wait_for 5 pidfile_alive
PID1=$(cat "$PIDFILE")

HAVE_LOG=0
if wait_for 5 log_has "daemon started"; then HAVE_LOG=1; fi
if [ "$HAVE_LOG" -eq 1 ]; then
    ok "запуск зарегистрирован в системном журнале"
else
    skip "журнал недоступен: выполните 'bash setup_syslog.sh' (проверки событий пропущены)"
fi

echo "== События в папках"
echo hello > "$WORK/a/${RUN}_one.txt"
cat "$WORK/a/${RUN}_one.txt" > /dev/null
echo more >> "$WORK/a/${RUN}_one.txt"
mv "$WORK/a/${RUN}_one.txt" "$WORK/a/${RUN}_two.txt"
rm "$WORK/a/${RUN}_two.txt"
echo x > "$WORK/b/${RUN}_inb.txt"
mkdir "$WORK/a/${RUN}_sub"
sleep 0.3
echo y > "$WORK/a/${RUN}_sub/${RUN}_deep.txt"

if [ "$HAVE_LOG" -eq 1 ]; then
    check "CREATE файла"                   logged "CREATE file $WORK/a/${RUN}_one.txt"
    check "MODIFY файла"                   logged "MODIFY file $WORK/a/${RUN}_one.txt"
    check "OPEN при чтении (cat)"          logged "OPEN file $WORK/a/${RUN}_one.txt"
    check "ACCESS при чтении (cat)"        logged "ACCESS file $WORK/a/${RUN}_one.txt"
    check "переименование (MOVED_FROM)"    logged "MOVED_FROM file $WORK/a/${RUN}_one.txt"
    check "переименование (MOVED_TO)"      logged "MOVED_TO file $WORK/a/${RUN}_two.txt"
    check "DELETE файла"                   logged "DELETE file $WORK/a/${RUN}_two.txt"
    check "вторая папка конфига (b)"       logged "CREATE file $WORK/b/${RUN}_inb.txt"
    check "создание подпапки"              logged "CREATE dir $WORK/a/${RUN}_sub"
    check "файл во вложенной подпапке"     logged "CREATE file $WORK/a/${RUN}_sub/${RUN}_deep.txt"
    echo z > "$WORK/c/${RUN}_notwatched.txt"
    sleep 1
    check "папка вне конфига не отслеживается" log_hasnot "${RUN}_notwatched"
else
    skip "проверки событий (нужен журнал)"
fi

echo "== SIGHUP: перечитывание конфига (демон уже сменил рабочий каталог на /)"
printf 'folder = c\nrecursive = yes\n' > "$WORK/test.conf"
kill -HUP "$PID1"
check "демон жив после SIGHUP" alive "$PID1"
if [ "$HAVE_LOG" -eq 1 ]; then
    check "SIGHUP зарегистрирован в журнале" logged "SIGHUP received"
    check "конфиг перечитан" logged "config reloaded"
    sleep 0.5
    echo 1 > "$WORK/c/${RUN}_newc.txt"
    echo 2 > "$WORK/a/${RUN}_olda.txt"
    check "новая папка c отслеживается"      logged "CREATE file $WORK/c/${RUN}_newc.txt"
    sleep 1
    check "старая папка a больше не отслеживается" log_hasnot "${RUN}_olda"
else
    skip "проверки журнала после SIGHUP (нужен журнал)"
fi

echo "== Повторный запуск (защита через pid-файл)"
( cd "$WORK" && "$BIN" test.conf )
check "старый экземпляр завершён" wait_for 7 dead "$PID1"
check "новый экземпляр записал pid-файл" wait_for 5 pidfile_alive
PID2=$(cat "$PIDFILE")
check "pid в файле изменился" test "$PID1" != "$PID2"

echo "== SIGTERM"
kill -TERM "$PID2"
check "процесс завершился" wait_for 5 dead "$PID2"
check "pid-файл удалён" wait_for 3 pidfile_gone
if [ "$HAVE_LOG" -eq 1 ]; then
    check "сообщение о выходе в журнале" logged "SIGTERM received"
fi

echo
echo "Итого: пройдено $PASS, провалено $FAIL, пропущено $SKIP"
[ "$FAIL" -eq 0 ]
