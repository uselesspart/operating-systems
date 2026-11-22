#!/bin/sh
set -eu

cd "$(dirname "$0")"

if [ "${1:-}" = "clean" ]; then
    rm -rf host_* test-logs
    exit 0
fi

for tool in cmake make; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Ошибка: не найден $tool" >&2
        exit 1
    fi
done

TESTS=OFF
[ "${1:-}" = "--test" ] && TESTS=ON

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/wolf_build.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

if ! cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS="$TESTS" >/dev/null; then
    echo "Ошибка: CMake не настроил сборку. Нужны компилятор C++17 и Qt 5/6 Widgets" >&2
    exit 1
fi
cmake --build "$BUILD_DIR" --parallel "$(nproc 2>/dev/null || echo 2)"

rm -f host_*
cp "$BUILD_DIR"/host_* .
echo "Собрано:" host_*

if [ "$TESTS" = ON ]; then
    set +e
    ctest --test-dir "$BUILD_DIR" --output-on-failure
    rc=$?
    set -e
    echo "Подробные логи тестов: $(pwd)/test-logs/"
    exit "$rc"
fi
