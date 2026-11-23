#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 01 сборка: make, build.sh, автоматическое добавление conn_*.cpp, -Wall -Werror"

COPY="$WS/project"
mkdir -p "$COPY"
cp -r "$LAB_DIR/src" "$LAB_DIR/CMakeLists.txt" "$LAB_DIR/Makefile" "$LAB_DIR/build.sh" "$COPY/"
before=$(ls -A "$COPY" | sort | tr '\n' ' ')

if make -C "$COPY" >"$WS/make.log" 2>&1; then
    note_pass "make собирает проект"
else
    note_fail "make собирает проект" "$(tail -n 20 "$WS/make.log")"
fi

for kind in pipe fifo sock; do
    assert_that "есть host_$kind и client_$kind" test -x "$COPY/host_$kind" -a -x "$COPY/client_$kind"
done

after=$(ls -A "$COPY" | grep -Ev '^(host|client)_' | sort | tr '\n' ' ')
assert_that "после сборки в каталоге нет файлов cmake" same "$before" "$after"
assert_that "нет CMakeCache.txt и CMakeFiles" test ! -e "$COPY/CMakeCache.txt" -a ! -e "$COPY/CMakeFiles"

make -C "$COPY" clean >/dev/null 2>&1
assert_that "make clean удаляет программы" test ! -e "$COPY/host_sock" -a ! -e "$COPY/client_sock"

sed 's/return "sock";/return "sockcopy";/' "$COPY/src/conn/conn_sock.cpp" >"$COPY/src/conn/conn_sockcopy.cpp"
if sh "$COPY/build.sh" >"$WS/build2.log" 2>&1; then
    note_pass "build.sh собирает проект с новым conn_sockcopy.cpp"
else
    note_fail "build.sh собирает проект с новым conn_sockcopy.cpp" "$(tail -n 20 "$WS/build2.log")"
fi
assert_that "для нового conn_sockcopy.cpp появились host_sockcopy и client_sockcopy" \
    test -x "$COPY/host_sockcopy" -a -x "$COPY/client_sockcopy"
rm -f "$COPY/src/conn/conn_sockcopy.cpp"

printf 'int lab2Unused() {\n    int unused = 0;\n    return 1;\n}\n' >"$COPY/src/util/Unused.cpp"
if sh "$COPY/build.sh" >"$WS/build3.log" 2>&1; then
    note_fail "предупреждение компилятора ломает сборку (-Wall -Werror)"
else
    if grep -q "Werror" "$WS/build3.log"; then
        note_pass "предупреждение компилятора ломает сборку (-Wall -Werror)"
    else
        note_fail "предупреждение компилятора ломает сборку (-Wall -Werror)" "$(tail -n 10 "$WS/build3.log")"
    fi
fi
rm -f "$COPY/src/util/Unused.cpp"

summary
