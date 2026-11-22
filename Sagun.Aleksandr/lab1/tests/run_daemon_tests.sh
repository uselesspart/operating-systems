#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(
    cd "$(dirname "${BASH_SOURCE[0]}")"
    pwd
)"

PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DAEMON="$PROJECT_DIR/mydaemon"
PID_FILE="/tmp/mydaemon.pid"

if [ "$(uname -s)" != "Linux" ]; then
    echo "These tests must run on Linux."
    exit 1
fi

if [ ! -x "$DAEMON" ]; then
    echo "Build the project first: bash build.sh"
    exit 1
fi

DAEMON="$(readlink -f "$DAEMON")"

if [ -L "$PID_FILE" ] ||
   { [ -e "$PID_FILE" ] &&
     { [ ! -f "$PID_FILE" ] || [ ! -O "$PID_FILE" ]; }; }; then
    echo "PID file must be a regular file owned by the current user."
    exit 1
fi

if [ -s "$PID_FILE" ]; then
    echo "PID file is not empty: $PID_FILE"
    echo "Stop your normal daemon before running these tests."
    echo "If it was killed, check the stale PID manually first."
    exit 1
fi

TEST_DIR="$(mktemp -d)"
CONFIG_FILE="$TEST_DIR/config.conf"
LOG_FILE="$TEST_DIR/reminders.log"
TEST_PIDS=()
CURRENT_PID=""
PASSED=0

fail()
{
    echo "[FAIL] $*" >&2
    exit 1
}

pass()
{
    PASSED=$((PASSED + 1))
    echo "[PASS] $*"
}

isDaemonRunning()
{
    local pid="$1"
    local executable

    [[ "$pid" =~ ^[1-9][0-9]*$ ]] || return 1

    executable="$(readlink "/proc/$pid/exe" 2>/dev/null)" || return 1
    [ "$executable" = "$DAEMON" ]
}

waitFor()
{
    local description="$1"
    shift

    local attempt

    for ((attempt = 0; attempt < 100; ++attempt)); do
        if "$@"; then
            return 0
        fi

        sleep 0.1
    done

    fail "Timeout: $description"
}

cleanup()
{
    local result="$?"
    local pid
    local attempt

    trap - EXIT INT TERM

    for pid in "${TEST_PIDS[@]}"; do
        if isDaemonRunning "$pid"; then
            kill -TERM "$pid" 2>/dev/null || true

            for ((attempt = 0; attempt < 100; ++attempt)); do
                if ! isDaemonRunning "$pid"; then
                    break
                fi

                sleep 0.1
            done

            if isDaemonRunning "$pid"; then
                echo "[FAIL] Test daemon did not stop; sending SIGKILL." >&2
                kill -KILL "$pid" 2>/dev/null || true
                result=1
            fi
        fi
    done

    if [ "$result" -ne 0 ]; then
        echo "Test files retained: $TEST_DIR" >&2
    else
        rm -rf -- "$TEST_DIR"
    fi

    exit "$result"
}

trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

hasNewPid()
{
    local previous="$1"
    local pid

    pid="$(cat "$PID_FILE" 2>/dev/null)" || return 1
    [ "$pid" != "$previous" ] || return 1
    isDaemonRunning "$pid" || return 1

    CURRENT_PID="$pid"
    return 0
}

startDaemon()
{
    local previous="${1:-}"

    "$DAEMON" "$CONFIG_FILE"
    waitFor "daemon startup" hasNewPid "$previous"
    TEST_PIDS+=("$CURRENT_PID")

    # PID записывается до установки обработчиков сигналов.
    # Даём демону закончить инициализацию.
    sleep 0.3
    isDaemonRunning "$CURRENT_PID" || fail "Daemon exited after startup"
}

hasStopped()
{
    ! isDaemonRunning "$1"
}

hasReminder()
{
    grep -Fxq -- "$1" "$LOG_FILE"
}

requireLogCount()
{
    local expected="$1"
    local actual

    actual="$(wc -l < "$LOG_FILE")"
    [ "$actual" -eq "$expected" ] ||
        fail "Expected $expected reminders, got $actual"
}

mkdir "$TEST_DIR/bin"
: > "$LOG_FILE"

# Подставной терминал записывает текст, полученный от демона.
cat > "$TEST_DIR/bin/x-terminal-emulator" <<'TERMINAL'
#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 6 ] ||
   [ "$1" != "-e" ] ||
   [ "$2" != "bash" ] ||
   [ "$3" != "-c" ] ||
   [ "$5" != "reminder" ]; then
    exit 1
fi

printf '%s\n' "$6" >> "$REMINDER_TEST_LOG"
TERMINAL

chmod +x "$TEST_DIR/bin/x-terminal-emulator"
export PATH="$TEST_DIR/bin:$PATH"
export REMINDER_TEST_LOG="$LOG_FILE"

printf 'interval=1\n' > "$CONFIG_FILE"

startDaemon
pass "Daemon starts and writes its PID"

kill -TERM "$CURRENT_PID"
waitFor "SIGTERM shutdown" hasStopped "$CURRENT_PID"
[ ! -s "$PID_FILE" ] || fail "PID file was not cleared"
pass "SIGTERM stops daemon and clears PID file"

startDaemon
OLD_PID="$CURRENT_PID"
startDaemon "$OLD_PID"
waitFor "previous daemon shutdown" hasStopped "$OLD_PID"
pass "Second instance replaces the previous daemon"

# Событие уже наступило: не нужно ждать следующей минуты.
EVENT_DATE_TIME="$(date -d '1 minute ago' '+%Y-%m-%d %H:%M')"
EVENT_DATE="${EVENT_DATE_TIME% *}"
EVENT_TIME="${EVENT_DATE_TIME#* }"

printf 'interval=1\nadd_event %s %s Reload reminder\n' \
    "$EVENT_DATE" "$EVENT_TIME" > "$CONFIG_FILE"

kill -HUP "$CURRENT_PID"
waitFor "configuration reload" hasReminder "Reload reminder"
isDaemonRunning "$CURRENT_PID" || fail "Daemon exited after SIGHUP"
[ "$(cat "$PID_FILE")" = "$CURRENT_PID" ] ||
    fail "PID changed after SIGHUP"

sleep 3
requireLogCount 1
pass "SIGHUP loads an event without restarting or repeating it"

REMINDER_TEXT='Проверить лабораторную: $HOME; $(echo test) "текст"'

(
    cd "$TEST_DIR"
    bash "$PROJECT_DIR/add_event.sh" \
        "$EVENT_DATE" "$EVENT_TIME" "$REMINDER_TEXT"
)

waitFor "event added by script" hasReminder "$REMINDER_TEXT"

sleep 3
requireLogCount 2
isDaemonRunning "$CURRENT_PID" || fail "Daemon exited after reminder"
pass "add_event.sh adds an event and daemon passes the exact text once"

echo "Passed: $PASSED, failed: 0"
