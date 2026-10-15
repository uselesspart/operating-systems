#!/bin/bash
# Последовательный запуск всех интеграционных тестов
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"

bash "$HERE/test_build.sh"
bash "$HERE/test_config.sh"
bash "$HERE/test_daemon.sh"
bash "$HERE/test_pidfile.sh"
bash "$HERE/test_signals.sh"

echo "Integration tests passed"