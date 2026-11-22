#!/usr/bin/env bash
set -euo pipefail
base=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d)
pid=''
cleanup() {
    if [[ -s $work/config.pid ]]; then
        read -r pid < "$work/config.pid" || true
        [[ $pid =~ ^[0-9]+$ ]] && kill -TERM "$pid" 2>/dev/null || true
    fi
    rm -rf -- "$work"
}
trap cleanup EXIT
bash "$base/build.sh"
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -I"$base/include" \
    "$base/src/ReminderEvent.cpp" "$base/src/ConfigLoader.cpp" \
    "$base/src/ReminderSchedule.cpp" "$base/tests/ReminderTests.cpp" -o "$work/unit"
"$work/unit" "$work/unit.conf"
export REMINDER_TEST_LOG="$work/messages"
cat > "$work/terminal" <<'SH'
#!/bin/sh
printf '%s\n' "$6" >> "$REMINDER_TEST_LOG"
SH
chmod +x "$work/terminal"
printf 'interval 1\nterminal %s\n' "$work/terminal" > "$work/config"
cd -- "$work"
"$base/reminder" config
read -r pid < config.pid
[[ $(readlink "/proc/$pid/cwd") == / ]]
for fd in 0 1 2; do [[ $(readlink "/proc/$pid/fd/$fd") == /dev/null ]]; done
printf 'PASS daemon context\n'
wait_message() {
    for ((i=0; i<100; ++i)); do
        if [[ -f $work/messages ]] && [[ $(wc -l < "$work/messages") -ge $1 ]]; then return; fi
        sleep 0.1
    done
    printf 'FAIL waiting for %s messages\n' "$1" >&2; exit 1
}
stamp=$(date -d '+3 seconds' '+%Y-%m-%d %H:%M:%S')
read -r day time <<< "$stamp"
text='literal $(touch SHOULD_NOT_EXIST); "quotes" and пробелы'
REMINDER_CONFIG="$work/config" bash "$base/add_event.sh" "$day" "$time" "$text"
wait_message 1
[[ $(cat messages) == "$text" && ! -e SHOULD_NOT_EXIST ]]
kill -HUP "$pid"
sleep 1.2
[[ $(wc -l < messages) -eq 1 ]]
printf 'PASS script, notification arguments, SIGHUP without duplicates\n'
# Broken reload must retain the previous scheduled reminder.
stamp=$(date -d '+4 seconds' '+%Y-%m-%d %H:%M:%S')
printf 'add_event %s retained\n' "$stamp" >> config
kill -HUP "$pid"
sleep 0.3
printf 'invalid directive\n' >> config
kill -HUP "$pid"
wait_message 2
[[ $(tail -n 1 messages) == retained ]]
printf 'PASS failed reload preserves schedule\n'
sed -i '$d' config
"$base/reminder" config
read -r replacement < config.pid
[[ $replacement != "$pid" ]]
for ((i=0; i<50; ++i)); do
    [[ ! -e /proc/$pid/exe ]] && break
    sleep 0.1
done
[[ ! -e /proc/$pid/exe ]]
pid=$replacement
printf 'PASS replacement through PID file\n'
kill -TERM "$pid"
for ((i=0; i<50; ++i)); do
    [[ ! -s config.pid ]] && break
    sleep 0.1
done
[[ ! -s config.pid ]]
printf 'PASS SIGTERM and PID cleanup\n'
# A PID file pointing at an unrelated program must not terminate it.
sleep 60 &
foreign=$!
printf '%s\n' "$foreign" > config.pid
if "$base/reminder" config; then
    kill "$foreign" 2>/dev/null || true
    printf 'FAIL accepted unrelated PID\n' >&2
    exit 1
fi
kill -0 "$foreign"
kill "$foreign"
wait "$foreign" 2>/dev/null || true
printf 'PASS unrelated process protected\n'
printf '999999999\n' > config.pid
"$base/reminder" config
printf 'PASS stale PID\n'
if bash "$base/add_event.sh" 2026-02-30 10:00 bad; then exit 1; fi
if bash "$base/add_event.sh" 2026-10-26 10:00 -x bad; then exit 1; fi
printf 'All tests passed\n'
