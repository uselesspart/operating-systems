#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
trap 'stop_all; remove_tmp' EXIT

CTL="$TEST_TMP/ctl"
W="$TEST_TMP/w"
CONF="$CTL/daemon.conf"
PIDF="$CTL/daemon.pid"
mkdir -p "$CTL" "$W"
touch "$W/keep.me"
echo data > "$W/data"
make_conf "$CONF" 86400 "$W keep.me"

init_journal

echo "[$TEST_NAME] SIGTERM при очень длинном интервале"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
FIRST="$DPID"
expect "PID-файл создан" test -f "$PIDF"

signal_daemon TERM "$FIRST"
wait_until 3 not_alive "$FIRST"
expect "процесс завершился быстро (менее 3 с при interval 86400)" not_alive "$FIRST"
expect_not "PID-файл удалён" test -e "$PIDF"
expect "данные при ignfile не тронуты" test -f "$W/data"
expect_journal_pid "SIGTERM записан в журнал" "$FIRST" "SIGTERM received, daemon is exiting"
expect_journal_pid "запись о завершении" "$FIRST" "daemon stopped"

echo "[$TEST_NAME] повторный запуск после штатного завершения"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
SECOND="$DPID"
expect_not "новый PID отличается от прежнего" test "$SECOND" = "$FIRST"
expect_eq "PID-файл содержит новый PID" "$(pid_of "$PIDF")" "$SECOND"
expect "новый процесс жив" is_ours "$SECOND"

signal_daemon TERM "$SECOND"
wait_until 3 not_alive "$SECOND"
expect "второй процесс завершился" not_alive "$SECOND"
expect_not "PID-файл удалён повторно" test -e "$PIDF"

finish
