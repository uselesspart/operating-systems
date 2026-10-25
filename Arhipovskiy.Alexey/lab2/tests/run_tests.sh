#!/usr/bin/env bash
#
# Использование:  bash tests/run_tests.sh               all
#                 bash tests/run_tests.sh unit chat     different tests
#
# Наборы:  unit      модульные тесты, тесты Conn и хоста (QtTest, через ctest)
#          chat      интеграционные: чат на настоящих host_* и client_*
#          failures  интеграционные: сбои и тайм-ауты
#          build     сборка через make

set -u

cd "$(dirname "$0")/.." || exit 2

ALL_SUITES=(unit chat failures build)
suites=("$@")
[ "${#suites[@]}" -gt 0 ] || suites=("${ALL_SUITES[@]}")
for suite in "${suites[@]}"; do
    case " ${ALL_SUITES[*]} " in
    *" $suite "*) ;;
    *)
        echo "Нет набора $suite, есть: ${ALL_SUITES[*]}" >&2
        exit 2
        ;;
    esac
done

JOBS=$(nproc 2>/dev/null || echo 2)
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT

echo "Сборка с тестами..."
if ! cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCHAT_BUILD_TESTS=ON >"$BUILD_DIR/build.log" 2>&1 ||
    ! cmake --build "$BUILD_DIR" --parallel "$JOBS" >>"$BUILD_DIR/build.log" 2>&1; then
    tail -n 30 "$BUILD_DIR/build.log" >&2
    echo "Сборка не удалась" >&2
    exit 1
fi

run_suite()
{
    case $1 in
    unit)
        echo "== Модульные тесты, тесты Conn и хоста (ctest)"
        (cd "$BUILD_DIR" && ctest --output-on-failure --parallel "$JOBS")
        ;;
    build)
        bash tests/integration/test_build.sh
        ;;
    *)
        CHAT_BIN_DIR="$BUILD_DIR" bash "tests/integration/test_$1.sh"
        ;;
    esac
}

passed=()
failed=()
for suite in "${suites[@]}"; do
    echo
    if run_suite "$suite"; then
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
