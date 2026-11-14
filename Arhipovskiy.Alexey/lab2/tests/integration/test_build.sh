#!/usr/bin/env bash
# Сборка: make собирает host_* и client_* для каждого conn_*.cpp без правки CMakeLists.txt,
# промежуточные файлы CMake не остаются в проекте, флаги -Wall -Werror, make clean

# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

prepare_copy()
{
    COPY="$TEST_ROOT/lab"
    BUILD_TMP="$TEST_ROOT/tmp"
    mkdir -p "$COPY" "$BUILD_TMP"
    cp -r "$LAB_DIR/src" "$LAB_DIR/CMakeLists.txt" "$LAB_DIR/Makefile" "$LAB_DIR/build.sh" "$COPY/"
}

run_make()
{
    (cd "$COPY" && TMPDIR="$BUILD_TMP" make "$@") >"$TEST_ROOT/build.out" 2>&1
    BUILD_STATUS=$?
}

# Новый тип связи: копия conn_sock.cpp со своим кодом типа
add_copy_type()
{
    sed 's/return "sock";/return "copy";/' "$COPY/src/conn/conn_sock.cpp" >"$COPY/src/conn/conn_copy.cpp"
}

all_built()
{
    local type
    for type in fifo pipe sock copy; do
        [ -x "$COPY/host_$type" ] && [ -x "$COPY/client_$type" ] || return 1
    done
}

no_cmake_leftovers()
{
    [ -z "$(find "$COPY" -name 'CMakeCache.txt' -o -name 'CMakeFiles' -o -name '*_autogen' -o -name '*.o' -o -name 'cmake_install.cmake')" ]
}

dir_is_empty() { [ -z "$(ls -A "$1")" ]; }
nothing_built() { ! ls "$COPY"/host_* "$COPY"/client_* >/dev/null 2>&1; }

test_make_builds_every_type()
{
    prepare_copy
    add_copy_type
    local cmake_before
    cmake_before=$(md5sum <"$COPY/CMakeLists.txt")
    run_make
    check "make завершился успешно" equals "$BUILD_STATUS" 0
    check "host_* и client_* для fifo, pipe, sock и нового conn_copy.cpp" all_built
    check "CMakeLists.txt не менялся" equals "$(md5sum <"$COPY/CMakeLists.txt")" "$cmake_before"
    check "в проекте нет файлов CMake" no_cmake_leftovers
    check "временный каталог сборки удалён" dir_is_empty "$BUILD_TMP"

    # shellcheck disable=SC2034  # read by require_host
    local TYPE=copy BIN_DIR=$COPY
    if require_host; then
        ok "host_copy запускается с новым типом связи"
    fi

    run_make clean
    check "make clean удаляет программы" nothing_built
}

test_flags()
{
    check "CMakeLists.txt включает -Wall -Werror" file_has "$LAB_DIR/CMakeLists.txt" "add_compile_options(-Wall -Werror)"
}

test_warning_breaks_build()
{
    prepare_copy
    printf '\nint unusedForTest()\n{\n    int unused;\n    return 0;\n}\n' >>"$COPY/src/util/Clock.cpp"
    run_make
    check "предупреждение компилятора ломает сборку" not equals "$BUILD_STATUS" 0
    check "в выводе есть unused-variable" file_has "$TEST_ROOT/build.out" "unused-variable"
    check "программы не созданы" nothing_built
    check "после ошибки временный каталог тоже удалён" dir_is_empty "$BUILD_TMP"
}

init_suite "Сборка" build
run_test test_make_builds_every_type "Сборка всех типов связи"
run_test test_flags "Флаги компиляции"
run_test test_warning_breaks_build "Сборка с предупреждением"
finish
