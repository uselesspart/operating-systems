#!/usr/bin/env bash

source "$(dirname "$0")/common.sh"

echo "### 03 порядок: сообщения показываются в порядке времени отправки"

public_lines() {
    grep -E '^\[[0-9:]+\] [^ ]+: m[0-9]+' "$1" | sed -E 's/^\[[0-9:]+\] //'
}

for kind in $KINDS; do
    echo "--- тип связи $kind"
    start_host "$kind" || continue
    for name in A B C; do
        start_client "$kind" "$name"
        expect_line "$kind: $name подключился" "$(out_of "$name")" "Ваш номер" >/dev/null
    done
    expected=""
    for round in 1 2 3 4 5; do
        for name in A B C; do
            say "$name" "m$round$name"
            expected+="$name: m$round$name"$'\n'
            sleep 0.07
        done
    done
    wait_for "$(out_of C)" "m5C" 6
    sleep 1
    for name in A B C; do
        got=$(public_lines "$(out_of "$name")")$'\n'
        if same "$got" "$expected"; then
            note_pass "$kind: у $name все 15 сообщений в порядке отправки"
        else
            note_fail "$kind: у $name все 15 сообщений в порядке отправки" "получено:
$got"
        fi
    done
    stop_host
    reset_clients
done

summary
