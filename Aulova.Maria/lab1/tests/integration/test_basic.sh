#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
trap 'stop_all; remove_tmp' EXIT

CTL="$TEST_TMP/ctl"
OUT="$TEST_TMP/outside"
W_CLEAN="$TEST_TMP/w_clean"
W_KEEP="$TEST_TMP/w_keep"
W_OTHER="$TEST_TMP/w_other"
SLOW="$TEST_TMP/slow"
mkdir -p "$CTL" "$OUT" "$W_CLEAN" "$W_KEEP" "$W_OTHER" "$SLOW"

echo precious > "$OUT/precious.txt"
fill_dir "$W_CLEAN"
ln -s "$OUT" "$W_CLEAN/link_to_dir"
ln -s "$OUT/precious.txt" "$W_CLEAN/link_to_file"
fill_dir "$W_KEEP"
touch "$W_KEEP/keep.me"
fill_dir "$W_OTHER"
touch "$W_OTHER/keep.me"

init_journal

echo "[$TEST_NAME] запуск и демонизация"
make_conf "$CTL/main.conf" 1 "$W_CLEAN keep.me" "$W_KEEP keep.me" "$W_OTHER .dont_clean"
start_daemon --pid-file "$CTL/main.pid" "$CTL/main.conf"
require_started
MAIN="$DPID"

expect_eq "код возврата запуска 0" "$START_RC" 0
expect "процесс демона есть в /proc" test -d "/proc/$MAIN"
expect "процесс — наш исполняемый файл" is_ours "$MAIN"
expect_eq "PID-файл содержит PID демона" "$(pid_of "$CTL/main.pid")" "$MAIN"
expect_eq "рабочий каталог демона — /" "$(readlink "/proc/$MAIN/cwd")" "/"
expect_eq "демон — лидер сеанса (SID = PID)" "$(proc_sid "$MAIN")" "$MAIN"
expect_eq "у демона нет управляющего терминала" "$(proc_tty "$MAIN")" "?"
expect_not "демон не является потомком тестового shell" test "$(proc_ppid "$MAIN")" = "$$"
for fd in 0 1 2; do
    expect_eq "дескриптор $fd указывает на /dev/null" "$(readlink "/proc/$MAIN/fd/$fd")" "/dev/null"
done

echo "[$TEST_NAME] очистка и ignfile"
wait_until 6 dir_empty "$W_CLEAN"
expect "w_clean очищен" dir_empty "$W_CLEAN"
expect "сам каталог w_clean сохранён" test -d "$W_CLEAN"
wait_until 6 dir_empty "$W_OTHER"
expect "w_other очищен (его ignfile другой)" dir_empty "$W_OTHER"
expect "w_keep: файл на месте" test -f "$W_KEEP/a.txt"
expect "w_keep: вложенный файл на месте" test -f "$W_KEEP/sub/deep/c.txt"
expect "w_keep: скрытый файл на месте" test -f "$W_KEEP/.hidden"
expect "w_keep: ignfile на месте" test -f "$W_KEEP/keep.me"
expect "цель символической ссылки (файл) цела" test -f "$OUT/precious.txt"
expect_eq "внешний каталог не изменился" "$(count_all "$OUT")" 1
expect_eq "содержимое внешнего файла цело" "$(cat "$OUT/precious.txt")" "precious"

echo "[$TEST_NAME] повторная проверка по интервалу 1 с"
echo late > "$W_CLEAN/late.txt"
wait_until 4 dir_empty "$W_CLEAN"
expect "новый файл удалён при следующей проверке" dir_empty "$W_CLEAN"
expect "w_keep по-прежнему не тронут" test -f "$W_KEEP/a.txt"

echo "[$TEST_NAME] точность интервала (interval 4)"
make_conf "$CTL/slow.conf" 4 "$SLOW keep.me"
echo initial > "$SLOW/initial.txt"
start_daemon --pid-file "$CTL/slow.pid" "$CTL/slow.conf"
require_started
SLOW_PID="$DPID"
wait_until 3 test ! -e "$SLOW/initial.txt"
expect "первая проверка выполняется сразу после запуска" test ! -e "$SLOW/initial.txt"
echo late > "$SLOW/late.txt"
sleep 1.5
expect "через 1.5 с при interval 4 файл ещё не удалён" test -e "$SLOW/late.txt"
wait_until 5 test ! -e "$SLOW/late.txt"
expect "к концу интервала файл удалён" test ! -e "$SLOW/late.txt"
expect "второй демон жив" is_ours "$SLOW_PID"

echo "[$TEST_NAME] журнал"
expect_journal "запись о запуске" "daemon started (PID $MAIN"
expect_journal "запись об очистке" "folder '$W_CLEAN': cleaned, removed"
expect_journal "запись о защитном файле" "folder '$W_KEEP': protective file present, nothing removed"

finish
