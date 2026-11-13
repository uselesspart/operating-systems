#!/bin/bash
# Общие функции для интеграционных тестов демона

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
TEST_NAME="$(basename "${BASH_SOURCE[1]}" .sh)"
DAEMON="$ROOT/reminder_daemon"
PIDFILE="/tmp/reminder_daemon.pid"

pass() {
    echo "[$TEST_NAME] PASS: $*"
}

fail() {
    echo "[$TEST_NAME] FAIL: $*" >&2
    # Показать, откуда именно был вызван fail() - файл:строка
    echo "  at $(basename "${BASH_SOURCE[1]}"):${BASH_LINENO[0]}" >&2
    exit 1
}

make_test_dir() {
    local dir
    dir="$(mktemp -d /tmp/reminder_test.XXXXXX)"
    cat > "$dir/reminder.conf" <<EOF
interval = 1
events_file = $dir/events.txt
EOF
    touch "$dir/events.txt"
    echo "$dir"
}

start_daemon() {
    local conf="$1"
    rm -f "$PIDFILE"
    "$DAEMON" "$conf"
    sleep 1
    [ -f "$PIDFILE" ] || fail "демон не создал pid-файл"
    cat "$PIDFILE"
}

stop_daemon() {
    if [ -f "$PIDFILE" ]; then
        local pid
        pid="$(cat "$PIDFILE" 2>/dev/null || true)"
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
            sleep 1
        fi
        rm -f "$PIDFILE"
    fi
}

assert_alive() { kill -0 "$1" 2>/dev/null || fail "процесс $1 не работает"; }
assert_dead()  { kill -0 "$1" 2>/dev/null && fail "процесс $1 всё ещё жив" || true; }

cleanup() { stop_daemon; }
trap cleanup EXIT