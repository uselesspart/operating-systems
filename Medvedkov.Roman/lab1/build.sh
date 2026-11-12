#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
temporary=$(mktemp -d)
trap 'rm -rf -- "$temporary"' EXIT
"${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -O2 -Iinclude \
    src/main.cpp src/ReminderEvent.cpp src/ConfigLoader.cpp src/ReminderSchedule.cpp \
    src/Daemon.cpp src/PidFile.cpp src/TerminalNotifier.cpp -o "$temporary/reminder"
mv -- "$temporary/reminder" ./reminder
printf 'Built: %s/reminder\n' "$PWD"
