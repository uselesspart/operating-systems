#!/bin/bash
# Сборка проекта. Все промежуточные файлы cmake лежат во временной папке
# и удаляются по завершении; готовые host_* остаются в папке проекта.
#   ./build.sh        — собрать
#   ./build.sh clean  — удалить собранные файлы и логи
set -e
cd "$(dirname "$0")"

if [ "$1" = "clean" ]; then
    rm -rf build host_* client_* chat_*.log
    exit 0
fi

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ipc_chat_build.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"
echo "Готово: $(ls host_* 2>/dev/null | tr '\n' ' ')"
