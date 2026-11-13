#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
trap 'stop_all; remove_tmp' EXIT

CTL="$TEST_TMP/ctl"
W="$TEST_TMP/w"
CONF="$CTL/daemon.conf"
PIDF="$CTL/daemon.pid"
mkdir -p "$CTL" "$W"
touch "$W/keep.me"
make_conf "$CONF" 1 "$W keep.me"
printf 'interval 0\n' > "$CTL/bad.conf"

pid_from_out() {
    sed -n 's/.*started, PID \([0-9][0-9]*\).*/\1/p' "$1" | tail -n 1
}

init_journal

echo "[$TEST_NAME] повторный запуск заменяет прежний экземпляр"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
OLD="$DPID"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
NEW="$DPID"
expect_not "новый PID отличается от старого" test "$NEW" = "$OLD"
expect "прежний экземпляр завершён" not_alive "$OLD"
expect "новый экземпляр работает" is_ours "$NEW"
expect_eq "PID-файл содержит новый PID" "$(pid_of "$PIDF")" "$NEW"
expect_journal_pid "старый экземпляр получил SIGTERM" "$OLD" "SIGTERM received"
expect_journal "запись об остановке прежнего экземпляра" \
    "previous instance (PID $OLD) terminated after SIGTERM"

echo "[$TEST_NAME] ошибочная конфигурация не останавливает работающий экземпляр"
start_daemon --pid-file "$PIDF" "$CTL/bad.conf"
expect_not "запуск с плохой конфигурацией отклонён" test "$START_RC" -eq 0
expect "работающий экземпляр не затронут" is_ours "$NEW"
expect_eq "PID-файл не изменился" "$(pid_of "$PIDF")" "$NEW"

echo "[$TEST_NAME] устаревший PID-файл после SIGKILL"
signal_daemon KILL "$NEW"
wait_until 3 not_alive "$NEW"
expect "PID-файл остался после SIGKILL" test -f "$PIDF"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
AFTER_KILL="$DPID"
expect "запуск после аварийного завершения успешен" is_ours "$AFTER_KILL"
expect_eq "PID-файл обновлён" "$(pid_of "$PIDF")" "$AFTER_KILL"
expect_journal "устаревший PID-файл опознан" "stale PID file '$PIDF'"

echo "[$TEST_NAME] повреждённый PID-файл"
signal_daemon TERM "$AFTER_KILL"
wait_until 5 not_alive "$AFTER_KILL"
printf 'garbage\n' > "$PIDF"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
expect_eq "PID-файл перезаписан корректным PID" "$(pid_of "$PIDF")" "$DPID"
signal_daemon TERM "$DPID"
wait_until 5 not_alive "$DPID"

echo "[$TEST_NAME] посторонний процесс в PID-файле без блокировки"
spawn_sleeper
VICTIM="$SLEEPER_PID"
echo "$VICTIM" > "$CTL/foreign.pid"
start_daemon --pid-file "$CTL/foreign.pid" "$CONF"
require_started
expect "посторонний процесс не получил сигнал" kill -0 "$VICTIM"
expect_eq "PID-файл содержит PID демона" "$(pid_of "$CTL/foreign.pid")" "$DPID"
signal_daemon TERM "$DPID"
wait_until 5 not_alive "$DPID"

echo "[$TEST_NAME] посторонний процесс в заблокированном PID-файле"
if command -v flock >/dev/null 2>&1; then
    spawn_sleeper
    VICTIM2="$SLEEPER_PID"
    LOCKED="$CTL/locked.pid"
    echo "$VICTIM2" > "$LOCKED"
    exec 9<>"$LOCKED"
    if flock -x -n 9; then
        start_daemon --pid-file "$LOCKED" "$CONF"
        expect_not "запуск отклонён" test "$START_RC" -eq 0
        expect "сообщение о постороннем процессе" contains "$START_OUT" "not an instance"
        expect "посторонний процесс жив" kill -0 "$VICTIM2"
        expect_eq "PID-файл не изменён" "$(pid_of "$LOCKED")" "$VICTIM2"
    else
        bad "не удалось взять блокировку для проверки"
    fi
    exec 9>&-
else
    echo "  SKIP утилита flock не найдена"
fi

echo "[$TEST_NAME] PID-файл — символическая ссылка"
printf 'precious\n' > "$TEST_TMP/victim.txt"
ln -s "$TEST_TMP/victim.txt" "$CTL/link.pid"
start_daemon --pid-file "$CTL/link.pid" "$CONF"
expect_not "запуск отклонён" test "$START_RC" -eq 0
expect "сообщение о ссылке" contains "$START_OUT" "symbolic link"
expect_eq "целевой файл не изменён" "$(cat "$TEST_TMP/victim.txt")" "precious"

echo "[$TEST_NAME] PID-файл — каталог"
mkdir "$CTL/dir.pid"
start_daemon --pid-file "$CTL/dir.pid" "$CONF"
expect_not "запуск отклонён" test "$START_RC" -eq 0
expect "сообщение об обычном файле" contains "$START_OUT" "not a regular file"

echo "[$TEST_NAME] каталог PID-файла не существует"
start_daemon --pid-file "$TEST_TMP/no_such_dir/d.pid" "$CONF"
expect_not "запуск отклонён" test "$START_RC" -eq 0
expect "причина указана" contains "$START_OUT" "cannot open"
expect_not "каталог не создан" test -e "$TEST_TMP/no_such_dir"
expect_eq "PID не сообщён" "$DPID" ""

echo "[$TEST_NAME] одновременный запуск двух экземпляров"
RACE="$CTL/race.pid"
"$BIN" --pid-file "$RACE" "$CONF" > "$TEST_TMP/r1.out" 2>&1 &
P1=$!
"$BIN" --pid-file "$RACE" "$CONF" > "$TEST_TMP/r2.out" 2>&1 &
P2=$!
wait "$P1" "$P2"
R1="$(pid_from_out "$TEST_TMP/r1.out")"
R2="$(pid_from_out "$TEST_TMP/r2.out")"
[ -n "$R1" ] && DAEMON_PIDS+=("$R1")
[ -n "$R2" ] && DAEMON_PIDS+=("$R2")
sleep 1
ALIVE=0
SURVIVOR=""
for p in $R1 $R2; do
    if is_ours "$p"; then
        ALIVE=$((ALIVE + 1))
        SURVIVOR="$p"
    fi
done
expect_eq "после гонки жив ровно один экземпляр" "$ALIVE" 1
expect_eq "PID-файл принадлежит выжившему" "$(pid_of "$RACE")" "$SURVIVOR"

finish
