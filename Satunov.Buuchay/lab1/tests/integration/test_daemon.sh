
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LAB_DIR="$(cd -- "$SCRIPT_DIR/../.." && pwd)"

PID_FILE="/tmp/os_lab1_daemon.pid"
TMP_DIR="$(mktemp -d)"

cleanup() {
    if [[ -s "$PID_FILE" ]]; then
        local pid
        pid="$(cat "$PID_FILE" 2>/dev/null || true)"

        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null || true
        fi
    fi

    rm -rf "$TMP_DIR"
    rm -f "$PID_FILE"
}

trap cleanup EXIT

wait_for_pid() {
    for _ in {1..30}; do
        if [[ -s "$PID_FILE" ]]; then
            local pid
            pid="$(cat "$PID_FILE")"

            if kill -0 "$pid" 2>/dev/null; then
                echo "$pid"
                return 0
            fi
        fi

        sleep 0.2
    done

    echo "Daemon did not start" >&2
    return 1
}

wait_for_missing() {
    for _ in {1..50}; do
        if [[ ! -e "$1" ]]; then
            return 0
        fi

        sleep 0.2
    done

    echo "Path should have been removed: $1" >&2
    return 1
}

assert_exists() {
    [[ -e "$1" ]] || {
        echo "Expected path does not exist: $1" >&2
        exit 1
    }
}

assert_missing() {
    [[ ! -e "$1" ]] || {
        echo "Path should be removed: $1" >&2
        exit 1
    }
}

rm -f "$PID_FILE"

tree_one="$TMP_DIR/tree_one"
tree_two="$TMP_DIR/tree_two"

mkdir -p "$tree_one/level1/level2"

touch "$tree_one/root.txt"
touch "$tree_one/level1/level2/payload.txt"

printf '%s 1\n' "$tree_one" > "$TMP_DIR/config"

cd "$LAB_DIR"
./daemon "$TMP_DIR/config"

pid="$(wait_for_pid)"

wait_for_missing "$tree_one/level1"
assert_exists "$tree_one/root.txt"

mkdir -p "$tree_two/level1/level2/level3"

touch "$tree_two/root.txt"
touch "$tree_two/level1/file1.txt"
touch "$tree_two/level1/level2/file2.txt"
touch "$tree_two/level1/level2/level3/payload.txt"

printf '%s 2\n' "$tree_two" > "$TMP_DIR/config"
kill -HUP "$pid"

wait_for_missing "$tree_two/level1/level2"
wait_for_missing "$tree_two/level1/file1.txt"

assert_exists "$tree_two/root.txt"
assert_exists "$tree_two/level1"

old_pid="$pid"

./build.sh
[[ ! -e build ]]

./daemon "$TMP_DIR/config"

new_pid=""

for _ in {1..30}; do
    if [[ -s "$PID_FILE" ]]; then
        candidate="$(cat "$PID_FILE")"

        if [[ "$candidate" != "$old_pid" ]] &&
           kill -0 "$candidate" 2>/dev/null; then
            new_pid="$candidate"
            break
        fi
    fi

    sleep 0.2
done

[[ -n "$new_pid" ]] || {
    echo "New daemon did not start" >&2
    exit 1
}

if kill -0 "$old_pid" 2>/dev/null; then
    echo "Previous daemon is still running" >&2
    exit 1
fi

pid="$new_pid"

kill -TERM "$pid"

for _ in {1..30}; do
    if [[ ! -e "$PID_FILE" ]]; then
        break
    fi

    sleep 0.2
done

[[ ! -e "$PID_FILE" ]] || {
    echo "PID file was not removed" >&2
    exit 1
}

echo "Integration tests passed"
