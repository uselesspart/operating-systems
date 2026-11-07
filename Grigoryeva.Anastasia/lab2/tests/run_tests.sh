#!/bin/bash
# Запуск всех тестов: сборка (хост без Qt - консольный режим), модульные и интеграционные тесты.
# Исполняемые файлы собираются во временный каталог и не затрагивают ./bin.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

echo "== Сборка (без GUI)"
./build.sh --no-gui --out "$OUT" > /dev/null
echo "Сборка выполнена"

echo
echo "== Модульные тесты"
FAIL=0
"$OUT/unit_rules" || FAIL=1
for u in "$OUT"/unit_conn_*; do
    echo -n "$(basename "$u"): "
    "$u" || FAIL=1
done
[ "$FAIL" -eq 0 ] || { echo "Модульные тесты провалены"; exit 1; }

echo
echo "== Интеграционные тесты"
bash tests/test_game.sh "$OUT"
