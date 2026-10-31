#!/bin/bash
# Последовательный запуск всех интеграционных тестов
# При падении отдельного теста продолжает остальные в конце печатает сводку и выходит с кодом 1, если хоть один упал
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"

TESTS=(
    test_build
    test_config
    test_daemon
    test_pidfile
    test_signals
)

PASSED=0
FAILED=0
FAILED_LIST=()

for t in "${TESTS[@]}"; do
    echo
    echo "$t.sh"
    if bash "$HERE/$t.sh"; then
        PASSED=$((PASSED + 1))
    else
        FAILED=$((FAILED + 1))
        FAILED_LIST+=("$t")
    fi
done

echo
if [ "$FAILED" -eq 0 ]; then
    echo "Integration tests: $PASSED/$PASSED passed"
    exit 0
else
    echo "Integration tests: $PASSED passed, $FAILED failed"
    for t in "${FAILED_LIST[@]}"; do
        echo "  X $t.sh"
    done
    exit 1
fi