#!/bin/bash
# Проверяет:
#  - ./build.sh завершается успешно и создаёт reminder_daemon
#  - после сборки не остаётся build/, build_tests/ и *.o
set -e

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

cd "$ROOT"

rm -f reminder_daemon
rm -rf build build_tests

./build.sh > /dev/null

[ -x reminder_daemon ] || { echo "FAIL: reminder_daemon не собран"; exit 1; }
[ ! -d build ]         || { echo "FAIL: каталог build/ не удалён после сборки"; exit 1; }
[ ! -d build_tests ]   || { echo "FAIL: каталог build_tests/ не удалён после сборки"; exit 1; }

if ls ./*.o >/dev/null 2>&1; then
    echo "FAIL: в корне остались .o-файлы"
    exit 1
fi

echo "test_build: PASSED"