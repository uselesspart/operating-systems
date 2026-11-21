#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.."

# Собираем тесты во временные файлы и потом удаляем их.
scheduler_test=$(mktemp /tmp/reminder-scheduler-test.XXXXXX)
daemon_test=$(mktemp /tmp/reminder-daemon-test.XXXXXX)
trap 'rm -f -- "$scheduler_test" "$daemon_test"' EXIT

g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic -Isrc \
    tests/test_scheduler.cpp src/Scheduler.cpp -o "$scheduler_test"
"$scheduler_test"

g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
    tests/test_daemon.cpp -o "$daemon_test"
"$daemon_test"
