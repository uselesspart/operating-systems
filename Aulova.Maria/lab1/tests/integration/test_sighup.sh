#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
trap 'stop_all; remove_tmp' EXIT

CTL="$TEST_TMP/ctl"
WA="$TEST_TMP/wa"
WB="$TEST_TMP/wb"
CONF="$CTL/daemon.conf"
PIDF="$CTL/daemon.pid"
mkdir -p "$CTL" "$WA" "$WB"
fill_dir "$WA"
touch "$WA/keep.me"
fill_dir "$WB"

init_journal

echo "[$TEST_NAME] запуск с относительными путями"
make_conf "$CONF" 1 "../wa keep.me"
START_DIR="$CTL" start_daemon --pid-file daemon.pid daemon.conf
require_started
expect_eq "PID-файл создан по относительному пути" "$(pid_of "$PIDF")" "$DPID"
sleep 2.5
expect "до SIGHUP wa защищён ignfile" test -f "$WA/a.txt"
expect "до SIGHUP wb не управляется демоном" test -f "$WB/a.txt"

echo "[$TEST_NAME] новая корректная конфигурация"
make_conf "$CONF" 2 "../wa other.name" "../wb keep.me"
signal_daemon HUP "$DPID"
wait_until 8 dir_empty "$WA"
wait_until 8 dir_empty "$WB"
expect "после SIGHUP wa очищен" dir_empty "$WA"
expect "после SIGHUP wb очищен" dir_empty "$WB"
expect "демон жив после SIGHUP" is_ours "$DPID"
expect_journal_pid "перезагрузка записана в журнал" "$DPID" \
    "configuration reloaded (interval 2 s, 2 folder rule(s))"

echo "[$TEST_NAME] ошибочная конфигурация"
printf 'interval abc\n../wa keep.me\n' > "$CONF"
signal_daemon HUP "$DPID"
expect_journal_pid "отказ от плохой конфигурации" "$DPID" \
    "configuration reload failed, keeping previous settings"
echo n > "$WA/new1"
echo n > "$WB/new2"
wait_until 8 dir_empty "$WA"
wait_until 8 dir_empty "$WB"
expect "прежние настройки действуют (wa)" dir_empty "$WA"
expect "прежние настройки действуют (wb)" dir_empty "$WB"
expect "демон жив" is_ours "$DPID"

echo "[$TEST_NAME] конфигурационный файл исчез"
mv "$CONF" "$CONF.bak"
signal_daemon HUP "$DPID"
expect_journal_pid "ошибка чтения файла по запомненному абсолютному пути" "$DPID" \
    "config file '$CONF'"
echo n > "$WA/new3"
wait_until 8 dir_empty "$WA"
expect "прежние настройки действуют без файла" dir_empty "$WA"

echo "[$TEST_NAME] конфигурация с опасным каталогом"
make_conf "$CONF" 1 "../wa keep.me" "$CTL keep.me"
signal_daemon HUP "$DPID"
expect_journal_pid "опасный каталог отвергнут" "$DPID" "is or contains protected path"
expect "конфигурация цела" test -f "$CONF"
expect "PID-файл цел" test -f "$PIDF"
echo n > "$WB/new4"
wait_until 8 dir_empty "$WB"
expect "прежние настройки действуют после отказа" dir_empty "$WB"

echo "[$TEST_NAME] возврат к корректной конфигурации"
make_conf "$CONF" 1 "../wa keep.me"
signal_daemon HUP "$DPID"
expect_journal_pid "новая конфигурация применена" "$DPID" \
    "configuration reloaded (interval 1 s, 1 folder rule(s))"
touch "$WA/keep.me"
echo x > "$WA/x"
echo y > "$WB/y"
sleep 3
expect "wa защищён" test -f "$WA/x"
expect "wb больше не управляется" test -f "$WB/y"
expect "демон всё ещё тот же" is_ours "$DPID"

finish
