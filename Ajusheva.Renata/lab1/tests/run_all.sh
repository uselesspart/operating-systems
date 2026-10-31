#!/usr/bin/env bash


set -u
export LC_ALL=C

cd "$(dirname "$0")/.." || exit 2

CXX=${CXX:-g++}
FLAGS="-std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror"

selected=()
if [ $# -eq 0 ]; then
    selected=(unit tests/integration/[0-9][0-9]_*.sh)
else
    for wish in "$@"; do
        if [ "$wish" = unit ]; then
            selected+=(unit)
            continue
        fi
        matches=(tests/integration/[0-9][0-9]_*"$wish"*.sh)
        if [ ! -e "${matches[0]}" ]; then
            echo "нет сценария '$wish' в tests/integration/" >&2
            exit 2
        fi
        selected+=("${matches[@]}")
    done
fi

SCRATCH=$(mktemp -d "${TMPDIR:-/tmp}/diskmond-tests.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

echo "### подготовка"
if ! sh build.sh >"$SCRATCH/build.txt" 2>&1; then
    cat "$SCRATCH/build.txt"
    echo "build.sh не смог собрать проект" >&2
    exit 1
fi
echo "  [+] ./build.sh собрал diskmond"

if ! $CXX $FLAGS -fPIC -shared tests/preload/syslog_capture.cpp -o "$SCRATCH/libsyslog_capture.so"; then
    echo "не собралась библиотека tests/preload/syslog_capture.cpp" >&2
    exit 1
fi
echo "  [+] собрана библиотека перехвата syslog"
export DISKMOND_CAPTURE_LIB="$SCRATCH/libsyslog_capture.so"

unit_tests() {
    echo "### модульные тесты (tests/unit)"

    local sources=() file
    for file in src/*.cpp; do
        if [ "$file" != src/main.cpp ]; then
            sources+=("$file")
        fi
    done
    # shellcheck disable=SC2086 # флаги намеренно разбиваются на слова
    $CXX $FLAGS -Isrc -Itests/unit tests/unit/*.cpp "${sources[@]}" -o "$SCRATCH/unit_tests" && "$SCRATCH/unit_tests"
}

run_one() {
    if [ "$1" = unit ]; then
        unit_tests
    else
        bash "$1"
    fi
}

broken=()
for item in "${selected[@]}"; do
    echo
    if ! run_one "$item"; then
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
