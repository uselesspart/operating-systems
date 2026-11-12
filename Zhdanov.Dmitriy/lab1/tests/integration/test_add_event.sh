#!/bin/bash
set -u

SCRIPT_DIR=$(cd "$(dirname "$0")/../.." && pwd)
ADD_EVENT=(bash "$SCRIPT_DIR/add_event.sh")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

export REMINDER_CONFIG="$TMP/events.conf"
export XDG_RUNTIME_DIR="$TMP"

FAILED=0

fail() {
    echo "FAIL: $*" >&2
    FAILED=1
}

expect_failure() {
    local description=$1
    shift
    if "$@" >/dev/null 2>&1; then
        fail "$description: команда завершилась успешно"
    fi
}

"${ADD_EVENT[@]}" 2035-05-10 12:30 "Пара в ГЗ Политеха" >/dev/null || fail "обычное событие не добавилось"
[ -f "$REMINDER_CONFIG" ] || fail "файл событий не создан"
EXPECTED=$(date -d "2035-05-10 12:30" +%s)
[ "$(cat "$REMINDER_CONFIG")" = "$EXPECTED none Пара в ГЗ Политеха" ] || fail "неверная строка события"

"${ADD_EVENT[@]}" 2035-05-10 13:00 -h "Выпить воды" >/dev/null || fail "событие с -h не добавилось"
EXPECTED=$(date -d "2035-05-10 13:00" +%s)
[ "$(tail -n 1 "$REMINDER_CONFIG")" = "$EXPECTED hour Выпить воды" ] || fail "неверный флаг -h"

"${ADD_EVENT[@]}" 2035-05-10 14:00 -d "Проверить почту" >/dev/null || fail "событие с -d не добавилось"
EXPECTED=$(date -d "2035-05-10 14:00" +%s)
[ "$(tail -n 1 "$REMINDER_CONFIG")" = "$EXPECTED day Проверить почту" ] || fail "неверный флаг -d"

"${ADD_EVENT[@]}" 2035-05-10 15:00 -w "Подвести итоги" >/dev/null || fail "событие с -w не добавилось"
EXPECTED=$(date -d "2035-05-10 15:00" +%s)
[ "$(tail -n 1 "$REMINDER_CONFIG")" = "$EXPECTED week Подвести итоги" ] || fail "неверный флаг -w"

TEXT='Сказать "привет" и 100%'
"${ADD_EVENT[@]}" 2035-05-11 09:15 "$TEXT" >/dev/null || fail "текст со спецсимволами не добавился"
REST=$(tail -n 1 "$REMINDER_CONFIG")
REST=${REST#* }
REST=${REST#* }
[ "$REST" = "$TEXT" ] || fail "текст со спецсимволами изменился"

BEFORE=$(wc -l < "$REMINDER_CONFIG")
expect_failure "несуществующая дата" "${ADD_EVENT[@]}" 2035-02-30 12:00 "Текст"
expect_failure "несуществующее время" "${ADD_EVENT[@]}" 2035-05-10 25:00 "Текст"
expect_failure "неверный формат даты" "${ADD_EVENT[@]}" 10.05.2035 12:00 "Текст"
expect_failure "неизвестный флаг" "${ADD_EVENT[@]}" 2035-05-10 12:00 -m "Текст"
expect_failure "пустой текст" "${ADD_EVENT[@]}" 2035-05-10 12:00 "   "
expect_failure "нет текста" "${ADD_EVENT[@]}" 2035-05-10 12:00
expect_failure "нет аргументов" "${ADD_EVENT[@]}"
expect_failure "перевод строки в тексте" "${ADD_EVENT[@]}" 2035-05-10 12:00 $'Первая\nВторая'
AFTER=$(wc -l < "$REMINDER_CONFIG")
[ "$BEFORE" -eq "$AFTER" ] || fail "после ошибок в файл попали лишние строки"

NO_NEWLINE="$TMP/no_newline.conf"
printf '%s' "1700000000 none Без перевода строки" > "$NO_NEWLINE"
REMINDER_CONFIG="$NO_NEWLINE" "${ADD_EVENT[@]}" 2035-05-12 10:00 "Добавлено" >/dev/null || fail "не удалось дописать в файл без перевода строки"
[ "$(wc -l < "$NO_NEWLINE")" -eq 2 ] || fail "строка склеилась с предыдущей"

if command -v flock >/dev/null 2>&1; then
    LOCK="$REMINDER_CONFIG.lock"
    exec 9>"$LOCK"
    flock 9
    (bash "$SCRIPT_DIR/add_event.sh" 2035-05-13 10:00 "Под блокировкой" >/dev/null 2>&1) &
    WAITER=$!
    sleep 0.3
    if tail -n 1 "$REMINDER_CONFIG" | grep -qF "Под блокировкой"; then
        fail "add_event не ждал освобождения блокировки"
    fi
    flock -u 9
    exec 9>&-
    wait "$WAITER" || fail "add_event не завершился после снятия блокировки"
    tail -n 1 "$REMINDER_CONFIG" | grep -qF "Под блокировкой" || fail "add_event не добавил событие после блокировки"
fi

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "OK"
