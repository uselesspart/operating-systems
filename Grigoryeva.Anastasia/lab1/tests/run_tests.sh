#!/bin/bash
# Запуск всех тестов: модульные (C++) + интеграционные (демон целиком).
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "== Сборка и запуск модульных тестов"
g++ -std=c++17 -Wall -Werror -Isrc tests/test_event.cpp src/Event.cpp -o "$TMP/test_event"
TZ=UTC "$TMP/test_event"
"$TMP/test_event"   

echo
echo "== Сборка демона"
./build.sh

echo
echo "== Интеграционные тесты"
bash tests/test_daemon.sh
