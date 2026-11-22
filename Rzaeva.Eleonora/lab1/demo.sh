#!/usr/bin/env bash
#
# Демонстрация работы демона: создаёт каталоги, запускает демон,
# показывает результат очистки и записи журнала, завершает демон.
#
set -euo pipefail
cd "$(dirname "$0")"

if [[ ! -x ./daemon ]]; then
    ./build.sh
fi

base=/tmp/rzaeva_demo
export RZAEVA_PID_FILE="$base/daemon.pid"

echo "== готовим каталоги в $base =="
rm -rf "$base"
mkdir -p "$base/protected" "$base/cleanme/sub"
touch "$base/protected/keep.txt"                # файл-«защита»
echo "data" > "$base/protected/should_stay.txt"
echo "data" > "$base/cleanme/file.txt"
echo "data" > "$base/cleanme/sub/nested.txt"

echo "== запускаем демон (конфиг ./daemon.conf, интервал 20 с) =="
./daemon ./daemon.conf
sleep 2

pid="$(cat "$RZAEVA_PID_FILE")"
echo "pid демона: $pid"

echo "== protected (есть keep.txt) =="; ls -A "$base/protected" || true
echo "== cleanme (нет .keep - должно быть пусто) =="; ls -A "$base/cleanme" || true

echo "== записи журнала =="
journalctl "_PID=$pid" -o cat --no-pager 2>/dev/null || true

echo "== завершаем демон по SIGTERM =="
kill -TERM "$pid"
sleep 1
journalctl "_PID=$pid" -o cat --no-pager 2>/dev/null | tail -2 || true
