#!/usr/bin/env bash
# Запуск всех тестов disk_monitor
#
# Использование:  sudo bash tests/run_tests.sh             все наборы
#                 sudo bash tests/run_tests.sh signals     только test_signals.sh
#
# Нужны: собранный проект (собирается здесь же), rsyslog с правилом
# rsyslog/30-disk_monitor.conf и права на чтение /var/log (root или группа adm)

set -u

cd "$(dirname "$0")/.." || exit 2

echo "Сборка..."
if ! sh build.sh >/dev/null; then
    echo "Сборка не удалась" >&2
    exit 1
fi

if [ "$#" -gt 0 ]; then
    suites=()
    for name in "$@"; do
        name=${name#tests/}
        name=${name#test_}
        name=${name%.sh}
        [ -f "tests/test_$name.sh" ] || { echo "Нет набора tests/test_$name.sh" >&2; exit 2; }
        suites+=("tests/test_$name.sh")
    done
else
    suites=(tests/test_build.sh tests/test_daemon.sh tests/test_config.sh tests/test_events.sh
        tests/test_recursive.sh tests/test_signals.sh tests/test_pidfile.sh tests/test_load.sh)
fi

passed=()
failed=()
for suite in "${suites[@]}"; do
    echo
    if bash "$suite"; then
        passed+=("$suite")
    else
        failed+=("$suite")
    fi
done

echo
echo "================================"
echo "Наборов пройдено: ${#passed[@]} из ${#suites[@]}"
for suite in ${failed[@]+"${failed[@]}"}; do
    echo "  не пройден: $suite"
done
[ "${#failed[@]}" -eq 0 ]
