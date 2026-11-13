#!/usr/bin/env bash
source "$(dirname "${BASH_SOURCE[0]}")/lib.sh"
trap 'stop_all; remove_tmp' EXIT

CTL="$TEST_TMP/ctl"
OUT="$TEST_TMP/outside"
W_OK="$TEST_TMP/w_ok"
W_LINKS="$TEST_TMP/w_links"
W_A="$TEST_TMP/w_a"
MOVABLE="$TEST_TMP/movable"
CONF="$CTL/daemon.conf"
PIDF="$CTL/daemon.pid"
mkdir -p "$CTL" "$OUT" "$W_LINKS" "$W_A"
echo precious > "$OUT/precious.txt"
fill_dir "$W_OK"
ln -s "$W_OK" "$TEST_TMP/link_to_w_ok"
ln -s "$CTL" "$TEST_TMP/link_to_ctl"
BEFORE_OK="$(count_all "$W_OK")"

expect_rejected() {
    local desc="$1" text="$2"
    shift 2
    make_conf "$CONF" 1 "$@"
    local ctl_before
    ctl_before="$(count_all "$CTL")"
    start_daemon --pid-file "$PIDF" "$CONF"
    expect_not "$desc: запуск отклонён" test "$START_RC" -eq 0
    expect "$desc: сообщение '$text'" contains "$START_OUT" "$text"
    expect_not "$desc: PID-файл не создан" test -e "$PIDF"
    expect_eq "$desc: w_ok не тронут" "$(count_all "$W_OK")" "$BEFORE_OK"
    expect_eq "$desc: ctl не тронут" "$(count_all "$CTL")" "$ctl_before"
    expect_eq "$desc: процесс не запущен" "$DPID" ""
}

echo "[$TEST_NAME] опасные и некорректные конфигурации отвергаются при запуске"
expect_rejected "каталог содержит конфиг и PID-файл" "protected path" "$TEST_TMP keep.me"
expect_rejected "каталог содержит конфиг" "protected path" "$CTL keep.me"
expect_rejected "ссылка на каталог с конфигом" "protected path" "$TEST_TMP/link_to_ctl keep.me"
expect_rejected "вложенные каталоги" "overlaps" "$W_OK keep.me" "$W_OK/sub keep.me"
expect_rejected "несуществующий каталог рядом с корректным" "missing" \
    "$W_OK keep.me" "$TEST_TMP/missing keep.me"
expect_rejected "ignfile со слешем" "invalid ignfile" "$W_OK sub/keep"
expect_rejected "ignfile — две точки" "invalid ignfile" "$W_OK .."
expect_rejected "дубликат через ссылку" "duplicate folder" \
    "$W_OK keep.me" "$TEST_TMP/link_to_w_ok keep.me"

echo "[$TEST_NAME] символические ссылки внутри очищаемого каталога"
ln -s "$OUT" "$W_LINKS/l_outside_dir"
ln -s "$OUT/precious.txt" "$W_LINKS/l_outside_file"
ln -s "$CONF" "$W_LINKS/l_config"
ln -s "$CTL" "$W_LINKS/l_ctl"
ln -s /nonexistent_target "$W_LINKS/l_dangling"
echo again > "$W_A/a.txt"
ln -s "$W_A" "$MOVABLE"

make_conf "$CONF" 1 "$W_LINKS keep.me" "$MOVABLE keep.me"
start_daemon --pid-file "$PIDF" "$CONF"
require_started
init_journal

wait_until 6 dir_empty "$W_LINKS"
expect "ссылки удалены" dir_empty "$W_LINKS"
expect "сам каталог w_links сохранён" test -d "$W_LINKS"
expect "цель ссылки на файл цела" test -f "$OUT/precious.txt"
expect_eq "внешний каталог не изменился" "$(count_all "$OUT")" 1
expect "конфигурация цела" test -f "$CONF"
expect "PID-файл цел" test -f "$PIDF"
expect_eq "w_ok (вне конфигурации) не тронут" "$(count_all "$W_OK")" "$BEFORE_OK"

echo "[$TEST_NAME] ссылка перенаправлена на опасный каталог после запуска"
wait_until 6 dir_empty "$W_A"
expect "до подмены w_a очищается через ссылку" dir_empty "$W_A"
ln -sfn "$CTL" "$MOVABLE"
echo precious > "$W_A/again.txt"
sleep 3
expect "каталог ctl не очищен" test -f "$CONF"
expect "PID-файл не удалён" test -f "$PIDF"
expect "w_a больше не очищается" test -f "$W_A/again.txt"
expect "демон жив" is_ours "$DPID"
expect_journal "подмена отвергнута и записана в журнал" "folder '$MOVABLE': rejected, nothing removed"

finish
