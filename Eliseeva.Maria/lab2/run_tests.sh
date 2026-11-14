#!/bin/bash
# Сборка + все тесты:
#   1) модульные тесты Conn / протокола / семафоров (ctest);
#   2) интеграционные тесты: настоящие host_* (Qt, платформа offscreen) с клиентами-процессами.
# Промежуточные файлы cmake лежат во временной папке и удаляются.
set -e
cd "$(dirname "$0")"

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ipc_chat_tests.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON ${CMAKE_EXTRA_ARGS}
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "===== Модульные тесты ====="
(cd "$BUILD_DIR" && ctest --output-on-failure)

rc=0
if ls host_* >/dev/null 2>&1; then
    echo
    echo "===== Интеграционные тесты (Qt offscreen) ====="
    for h in host_*; do
        t="${h#host_}"
        echo "--- тип связи: $t"
        bash tests/smoke_test.sh "$t" || rc=1
    done
else
    echo "host_* не собраны (нет Qt) — интеграционные тесты пропущены"
fi

echo
if [ $rc -eq 0 ]; then echo "ВСЕ ТЕСТЫ ПРОЙДЕНЫ"; else echo "ЕСТЬ ПРОВАЛЕННЫЕ ТЕСТЫ"; fi
exit $rc
