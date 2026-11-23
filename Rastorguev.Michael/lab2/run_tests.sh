#!/usr/bin/env bash

cd "$(dirname "$0")" || exit 2
BUILD=build_tests
rm -rf "$BUILD"

if ! cmake -S . -B "$BUILD" -DBUILD_TESTS=ON >"$BUILD.log" 2>&1 ||
   ! cmake --build "$BUILD" --target tests -- -j"$(nproc)" >>"$BUILD.log" 2>&1; then
    cat "$BUILD.log"; rm -f "$BUILD.log"
    echo "Сборка тестов не удалась"; exit 2
fi
rm -f "$BUILD.log"

cd "$BUILD/bin" || exit 2
tests=$(ls test_* | sort)
for t in $tests; do
    ( "./$t" >"$t.out" 2>"$t.err"; echo $? >"$t.rc" ) &
done
wait

failed=0
for t in $tests; do
    echo "=== $t ==="
    cat "$t.out"
    if [ "$(cat "$t.rc")" != "0" ]; then
        failed=1
        echo "--- stderr ($t) ---"; tail -n 20 "$t.err"
    fi
done

cd ../..
[ "$1" != "--keep" ] && rm -rf "$BUILD"
echo
if [ $failed -eq 0 ]; then echo "ВСЕ ТЕСТЫ ПРОЙДЕНЫ"; else echo "ЕСТЬ ПРОВАЛЕННЫЕ ТЕСТЫ"; fi
exit $failed
