#!/usr/bin/env bash
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 2

if [ "$(id -u)" -eq 0 ]; then
    echo "Тесты нельзя запускать от root" >&2
    exit 2
fi

UNIT_DIR="$(mktemp -d /tmp/lab1_unit.XXXXXX)" || exit 2
trap 'rm -rf -- "$UNIT_DIR"' EXIT

FAILED_STEPS=()

step() {
    local name="$1"
    shift
    echo
    echo "==== $name"
    if "$@"; then
        echo "---- OK: $name"
    else
        echo "---- FAIL: $name" >&2
        FAILED_STEPS+=("$name")
    fi
}

unit() {
    local name="$1"
    shift
    g++ -std=c++17 -Wall -Werror -g -Iinclude "tests/$name.cpp" "$@" -o "$UNIT_DIR/$name" &&
        "$UNIT_DIR/$name"
}

if ! ./build.sh; then
    echo "Сборка не удалась, тесты не запускались" >&2
    exit 1
fi

step "unit_config" unit unit_config src/Config.cpp
step "unit_cleaner" unit unit_cleaner src/SafeCleaner.cpp
step "unit_pidfile" unit unit_pidfile src/PidFile.cpp
step "unit_daemon" unit unit_daemon src/Daemon.cpp src/Config.cpp src/SafeCleaner.cpp \
    src/PidFile.cpp src/Logger.cpp

for script in tests/integration/test_*.sh; do
    step "$(basename "$script")" bash "$script"
done

echo
leftovers="$(ls -d /tmp/lab1_it.* /tmp/lab1_test.* /tmp/lab1_unit.* 2>/dev/null | grep -v "^$UNIT_DIR\$" || true)"
if [ -n "$leftovers" ]; then
    echo "ВНИМАНИЕ: остались временные каталоги:" >&2
    echo "$leftovers" >&2
    FAILED_STEPS+=("temp-cleanup")
fi

if [ "${#FAILED_STEPS[@]}" -eq 0 ]; then
    echo "ВСЕ ТЕСТЫ ПРОЙДЕНЫ"
    exit 0
fi
echo "ПРОВАЛЕНО: ${FAILED_STEPS[*]}" >&2
exit 1
