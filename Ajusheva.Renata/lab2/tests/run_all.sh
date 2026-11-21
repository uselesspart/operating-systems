#!/usr/bin/env bash

set -u

cd "$(dirname "$0")/.." || exit 2

selected=()
if [ $# -eq 0 ]; then
    selected=(unit tests/integration/[0-9][0-9]_*.sh)
else
    for wish in "$@"; do
        if [ "$wish" = unit ]; then
            selected+=(unit)
            continue
        fi
        matches=(tests/integration/*"$wish"*.sh)
        if [ ! -e "${matches[0]}" ]; then
            echo "нет сценария '$wish' в tests/integration/" >&2
            exit 2
        fi
        selected+=("${matches[@]}")
    done
fi

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/lab2-tests.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo "### подготовка: сборка с тестами"
if ! cmake -S . -B "$SCRATCH/build" -DCMAKE_BUILD_TYPE=Release -DLAB2_TESTS=ON >"$SCRATCH/build.log" 2>&1 ||
    ! cmake --build "$SCRATCH/build" --parallel "$(nproc 2>/dev/null || echo 2)" >>"$SCRATCH/build.log" 2>&1; then
    tail -n 40 "$SCRATCH/build.log" >&2
    echo "сборка не удалась" >&2
    exit 1
fi
echo "  [+] собрано в $SCRATCH/build"
export LAB2_BIN="$SCRATCH/build"

run_unit() {
    echo "### модульные тесты"
    local failed=0 program
    for program in "$LAB2_BIN"/tests/unit_tests "$LAB2_BIN"/tests/link_tests_*; do
        echo "--- $(basename "$program")"
        if ! QT_QPA_PLATFORM=offscreen "$program"; then
            failed=1
        fi
    done
    return "$failed"
}

broken=()
for item in "${selected[@]}"; do
    echo
    if [ "$item" = unit ]; then
        run_unit || broken+=(unit)
    elif ! bash "$item"; then
        broken+=("$(basename "$item")")
    fi
done

echo
echo "================================================"
if [ "${#broken[@]}" -eq 0 ]; then
    echo "Результат: пройдено всё (${#selected[@]} из ${#selected[@]})"
else
    echo "Результат: не пройдено ${#broken[@]} из ${#selected[@]}: ${broken[*]}"
fi
[ "${#broken[@]}" -eq 0 ]
