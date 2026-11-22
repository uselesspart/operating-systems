#!/usr/bin/env bash
# Быстрая демонстрация: создаёт тестовые папки, запускает демон, показывает результат.
set -euo pipefail
cd "$(dirname "$0")"
[ -x ./daemon ] || ./build.sh

rm -rf /tmp/test_a /tmp/test_b
mkdir -p /tmp/test_a/sub1/deep /tmp/test_a/sub2 /tmp/test_b/keep_me
echo data > /tmp/test_a/file1.txt
echo data > /tmp/test_a/sub1/inner.txt
echo data > /tmp/test_b/file2.txt

echo "=== До запуска ==="; find /tmp/test_a /tmp/test_b | sort

./daemon daemon.conf
sleep 1

echo; echo "=== После первой проверки (подпапки /tmp/test_a удалены, /tmp/test_b не тронута) ==="
find /tmp/test_a /tmp/test_b | sort

kill -TERM "$(cat /tmp/dirclean_daemon.pid)"
sleep 0.3
echo; echo "Демон остановлен."
