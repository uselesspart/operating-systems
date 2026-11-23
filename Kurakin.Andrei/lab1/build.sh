#!/usr/bin/env bash
set -euo pipefail

cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

# Сразу собираем готовый бинарник, промежуточных .o-файлов не будет.
g++ -std=c++17 -O2 -Wall -Wextra -Werror -pedantic \
    src/main.cpp src/Config.cpp src/Scheduler.cpp src/ReminderDaemon.cpp \
    -o reminder_daemon

echo "Built: $(pwd)/reminder_daemon"
