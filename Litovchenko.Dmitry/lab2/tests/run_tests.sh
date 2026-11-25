#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."

TYPES=$(ls src/conn_*.cpp | sed 's/.*conn_\(.*\)\.cpp/\1/')
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
export QT_QPA_PLATFORM=offscreen

passed=0; failed=0
ok()   { echo "[  OK  ] $1"; passed=$((passed + 1)); }
fail() { echo "[ FAIL ] $1"; failed=$((failed + 1)); sed 's/^/    | /' "$OUT/log" 2>/dev/null | tail -15; }

state() { awk '{print $3}' "/proc/$1/stat" 2>/dev/null; }

wait_exit() {
    for _ in $(seq $(($2 * 10))); do
        local s; s=$(state "$1")
        [ -z "$s" ] || [ "$s" = Z ] && return 0
        sleep 0.1
    done
    return 1
}

no_leftovers() {
    ! ls /tmp/wolf_"$1"_* /tmp/wolf_"$1".sock /dev/mqueue/wolf_"$1"_* > /dev/null 2>&1
}

./build.sh "$@" > "$OUT/log" 2>&1 && ok "build" || { fail "build"; exit 1; }
./tests/test_game > "$OUT/log" 2>&1 && ok "game rules" || fail "game rules"

./host_mq --bogus > "$OUT/log" 2>&1; [ $? -eq 2 ] && ok "bad arguments" || fail "bad arguments"

for t in $TYPES; do
    ./host_"$t" --no-gui --rounds 20 --seed 3 > "$OUT/log" 2>&1 &
    pid=$!
    wait $pid; rc=$?
    if [ $rc -eq 0 ] && grep -q '^round=1 ' "$OUT/log" && grep -q 'козлёнок завершился, код 0' "$OUT/log" &&
        no_leftovers $pid; then ok "$t: 20 rounds over IPC, files removed"; else fail "$t: 20 rounds over IPC, files removed"; fi

    ./host_"$t" --no-gui --wolf 1 --seed 5 --rounds 1000 > "$OUT/log" 2>&1
    if grep -q 'игра окончена: козлёнок мёртв два хода подряд' "$OUT/log" &&
        tail -n +1 "$OUT/log" | grep '^round=' | tail -2 | grep -c 'after=dead' | grep -q 2; then
        ok "$t: game ends after two dead rounds"; else fail "$t: game ends after two dead rounds"; fi

    ./host_"$t" --no-gui --wolf 50 > "$OUT/log" 2>&1 &
    pid=$!; sleep 0.5
    goat=$(pgrep -P $pid)
    kill -STOP "$goat"
    if wait_exit $pid 8 && grep -q 'связь разорвана' "$OUT/log" && wait_exit "$goat" 1; then
        ok "$t: frozen goat -> wolf stops it after 5 s"; else fail "$t: frozen goat -> wolf stops it after 5 s"; fi
    kill -9 $pid "$goat" 2> /dev/null; wait $pid 2> /dev/null

    ./host_"$t" --no-gui --wolf 50 > "$OUT/log" 2>&1 &
    pid=$!; sleep 0.5
    goat=$(pgrep -P $pid)
    kill -STOP $pid
    if wait_exit "$goat" 10 && grep -q 'волк не ответил за 8 с' "$OUT/log"; then
        ok "$t: frozen wolf -> goat exits after 3 + 5 s"; else fail "$t: frozen wolf -> goat exits after 3 + 5 s"; fi
    kill -9 $pid; wait $pid 2> /dev/null
    rm -f /tmp/wolf_${pid}_* /tmp/wolf_${pid}.sock

    ./host_"$t" --no-gui --wolf 50 > "$OUT/log" 2>&1 &
    pid=$!; sleep 0.5
    kill -INT $pid
    if wait_exit $pid 7 && grep -q 'игра остановлена' "$OUT/log" && no_leftovers $pid; then
        ok "$t: SIGINT stops the game and removes IPC files"; else fail "$t: SIGINT stops the game and removes IPC files"; fi

    timeout 30 ./host_"$t" --rounds 2 > "$OUT/log" 2>&1
    if [ $? -eq 0 ] && grep -q 'козлёнок завершился, код 0' "$OUT/log"; then
        ok "$t: GUI (offscreen), 2 rounds with automatic wolf"; else fail "$t: GUI (offscreen), 2 rounds with automatic wolf"; fi
done

echo
echo "passed: $passed, failed: $failed"
[ $failed -eq 0 ]
