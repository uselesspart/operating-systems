#!/bin/bash
. "$(dirname "$0")/lib.sh"

suite "Сборка: Makefile, build.sh, CMakeLists"

copy_project() {
    COPY="$CASE_DIR/project"
    mkdir -p "$COPY"
    (cd "$PROJECT_DIR" && tar --exclude='./test-logs' --exclude='./host_*' \
        --exclude='./build' --exclude='./cmake-build-*' -cf - .) | (cd "$COPY" && tar -xf -)
}

case_B01() {
    copy_project
    step "make в чистой копии проекта"
    mkdir -p "$CASE_DIR/tmp"
    (cd "$COPY" && TMPDIR="$CASE_DIR/tmp" make) > "$CASE_DIR/make.out" 2>&1
    expect_eq "make завершился успешно" "$?" 0
    local t
    for t in pipe fifo sock; do
        expect_true "собран host_$t" test -x "$COPY/host_$t"
    done
    expect_eq "client_* не собираются для родственной схемы" \
        "$(ls "$COPY" | grep -c '^client_')" 0
    expect_eq "промежуточных файлов CMake в проекте нет" \
        "$(find "$COPY" \( -name CMakeCache.txt -o -name CMakeFiles -o -name '*.o' \) | wc -l)" 0
    expect_eq "временный каталог сборки удалён" "$(ls -A "$CASE_DIR/tmp" | wc -l)" 0
    step "make clean"
    (cd "$COPY" && make clean) >> "$CASE_DIR/make.out" 2>&1
    expect_eq "программы удалены" "$(ls "$COPY" | grep -c '^host_')" 0
}
run_case B01 "make собирает host_* для pipe, fifo, sock и убирает за собой" case_B01

case_B02() {
    copy_project
    step "добавляем conn_unix.cpp (копия conn_sock.cpp с другим typeCode)"
    sed 's/return "sock";/return "unix";/' "$COPY/src/conn/conn_sock.cpp" > "$COPY/src/conn/conn_unix.cpp"
    local before
    before="$(md5sum < "$COPY/CMakeLists.txt")"
    (cd "$COPY" && make) > "$CASE_DIR/make.out" 2>&1
    expect_eq "make завершился успешно" "$?" 0
    expect_true "автоматически появился host_unix" test -x "$COPY/host_unix"
    expect_eq "client_unix не появился" "$(ls "$COPY" | grep -c '^client_')" 0
    expect_eq "CMakeLists.txt не менялся" "$(md5sum < "$COPY/CMakeLists.txt")" "$before"
}
run_case B02 "Новый conn_<тип>.cpp даёт host_<тип> без правки CMakeLists" case_B02

case_B03() {
    copy_project
    step "добавляем файл с неиспользуемой переменной"
    printf 'int warn_me() {\n    int unused = 0;\n    return 1;\n}\n' > "$COPY/src/util/warning_probe.cpp"
    (cd "$COPY" && make) > "$CASE_DIR/make.out" 2>&1
    expect_ne "сборка не прошла: предупреждение стало ошибкой" "$?" 0
    expect_true "в выводе компилятора — -Werror=unused-variable" \
        file_has "$CASE_DIR/make.out" "Werror=unused-variable"
    expect_true "новый .cpp подхвачен сборкой автоматически" \
        file_has "$CASE_DIR/make.out" "warning_probe.cpp"
}
run_case B03 "Флаги -Wall -Werror: предупреждение ломает сборку" case_B03

finish
