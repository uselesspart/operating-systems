#!/usr/bin/env bash
# Сборка: build.sh даёт один исполняемый файл, -Wall -Werror, промежуточные файлы не остаются

# shellcheck source-path=SCRIPTDIR source=common.sh
. "$(dirname "$0")/common.sh"


copy_sources() {
    PROJECT="$WS/project"
    BUILD_TEMP="$WS/tmp"
    mkdir -p "$PROJECT" "$BUILD_TEMP"
    cp -r "$LAB_DIR/src" "$LAB_DIR/build.sh" "$PROJECT/"
}

run_build() {
    (cd "$PROJECT" && TMPDIR="$BUILD_TEMP" "$@" sh build.sh) >"$WS/build.txt" 2>&1
    BUILD_CODE=$?
}

compilers_started() { grep -q '^  CXX ' "$WS/build.txt" 2>/dev/null; }

normal_build() {
    copy_sources
    run_build
    local sources=("$PROJECT"/src/*.cpp) contents=("$PROJECT"/*)
    assert_that "build.sh отработал с кодом 0" same "$BUILD_CODE" 0
    assert_that "на выходе исполняемый ./diskmond" test -x "$PROJECT/diskmond"
    assert_that "скомпилирован каждый файл из src/" same "$(grep -c '^  CXX ' "$WS/build.txt")" "${#sources[@]}"
    assert_that "рядом с исходниками добавился только diskmond" same "${contents[*]#"$PROJECT/"}" "build.sh diskmond src"
    assert_that "ни одного .o ни в проекте, ни во временном каталоге" \
        same "$(find "$PROJECT" "$BUILD_TEMP" -name '*.o' | wc -l)" 0
    assert_that "временный каталог сборки убран" empty_dir "$BUILD_TEMP"
    assert_that "собранная программа отвечает на --help" muted "$PROJECT/diskmond" --help
}

compiler_flags() {
    assert_that "в CXXFLAGS есть -Wall" has_line "$LAB_DIR/build.sh" '^CXXFLAGS=.*-Wall'
    assert_that "в CXXFLAGS есть -Werror" has_line "$LAB_DIR/build.sh" '^CXXFLAGS=.*-Werror'
}

warning_is_fatal() {
    copy_sources
    printf '\nstatic int helperNobodyCalls()\n{\n    int unused;\n    return 0;\n}\n' >>"$PROJECT/src/main.cpp"
    run_build
    assert_that "из-за одного предупреждения сборка не удалась" differ "$BUILD_CODE" 0
    assert_that "компилятор назвал причину: -Werror=unused-..." has_line "$WS/build.txt" 'Werror=unused'
    assert_that "старого или частичного diskmond нет" test ! -e "$PROJECT/diskmond"
    assert_that "временный каталог убран и после неудачи" empty_dir "$BUILD_TEMP"
}

interrupted_build() {
    if ! command -v setsid >/dev/null 2>&1; then
        note_skip "прерывание сборки: нет утилиты setsid"
        return
    fi
    copy_sources
    
    (cd "$PROJECT" && TMPDIR="$BUILD_TEMP" exec setsid sh build.sh) >"$WS/build.txt" 2>&1 &
    local builder=$! deadline=$((SECONDS + PATIENCE))
    until compilers_started || [ "$SECONDS" -ge "$deadline" ]; do
        sleep 0.05
    done
    kill -TERM -- "-$builder" 2>/dev/null
    wait "$builder"
    assert_that "сборку прервали посреди компиляции" same "$?" 143
    assert_that "после прерывания временный каталог тоже убран" empty_dir "$BUILD_TEMP"
    assert_that "исполняемый файл не появился" test ! -e "$PROJECT/diskmond"
    sleep 0.3
    assert_that "в фоне не осталось компиляторов" negated muted pgrep -f "$BUILD_TEMP"
}

with_clang() {
    if ! command -v clang++ >/dev/null 2>&1; then
        note_skip "сборка через clang++: clang++ не установлен"
        return
    fi
    copy_sources
    run_build env CXX=clang++
    assert_that "CXX=clang++ ./build.sh тоже собирает без единого предупреждения" same "$BUILD_CODE" 0
    assert_that "временный каталог убран" empty_dir "$BUILD_TEMP"
}

begin_suite "Сборка" no-daemon
scenario normal_build "Обычная сборка"
scenario compiler_flags "Флаги компилятора"
scenario warning_is_fatal "Предупреждение равно ошибке"
scenario interrupted_build "Прерванная сборка"
scenario with_clang "Сборка другим компилятором"
end_suite
