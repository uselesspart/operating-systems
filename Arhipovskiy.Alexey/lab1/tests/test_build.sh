#!/usr/bin/env bash
# Сборка: build.sh, флаги -Wall -Werror, очистка промежуточных файлов

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

prepare_copy()
{
    COPY="$TEST_ROOT/lab"
    BUILD_TMP="$TEST_ROOT/tmp"
    mkdir -p "$COPY" "$BUILD_TMP"
    cp -r "$LAB_DIR/src" "$LAB_DIR/include" "$LAB_DIR/build.sh" "$COPY/"
}

run_build()
{
    (cd "$COPY" && TMPDIR="$BUILD_TMP" sh build.sh) >"$TEST_ROOT/build.out" 2>&1
    BUILD_STATUS=$?
}

test_build_succeeds()
{
    prepare_copy
    run_build
    check "build.sh завершился успешно" equals "$BUILD_STATUS" 0
    check "собран один исполняемый файл disk_monitor" test -x "$COPY/disk_monitor"
    check "временный каталог с объектными файлами удалён" dir_is_empty "$BUILD_TMP"
    check "в каталоге проекта нет .o" equals "$(find "$COPY" -name '*.o' | wc -l)" 0
    check "программа запускается (--help)" quietly "$COPY/disk_monitor" --help
}

test_build_flags()
{
    check "build.sh компилирует с -Wall -Werror" file_has "$LAB_DIR/build.sh" '-Wall -Werror'
}

test_warning_breaks_build()
{
    prepare_copy
    printf '\nint unusedForTest()\n{\n    int unused;\n    return 0;\n}\n' >>"$COPY/src/main.cpp"
    run_build
    check "предупреждение компилятора ломает сборку (-Werror)" not equals "$BUILD_STATUS" 0
    check "в выводе есть unused-variable" file_has "$TEST_ROOT/build.out" 'unused-variable'
    check "исполняемый файл не создан" test ! -e "$COPY/disk_monitor"
    check "после ошибки временный каталог тоже удалён" dir_is_empty "$BUILD_TMP"
}

init_suite "Сборка" build
run_test test_build_succeeds "Успешная сборка"
run_test test_build_flags "Флаги компиляции"
run_test test_warning_breaks_build "Сборка с предупреждением"
finish
