set -u

SUITE_NAME="$(basename "$0" .sh)"
BIN_DIR="$(realpath "${1:?первый аргумент — каталог с host_*}")"
PROJECT_DIR="$(realpath "$(dirname "$0")/../..")"
TYPES="${TEST_TYPES:-pipe fifo sock}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

LOG_DIR="${TEST_LOG_DIR:-$PWD/test-logs}"
mkdir -p "$LOG_DIR"
LOG_FILE="$LOG_DIR/$SUITE_NAME.log"
ARTIFACT_DIR="$LOG_DIR/$SUITE_NAME"
rm -rf "$ARTIFACT_DIR"
: > "$LOG_FILE"

if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]; then
    C_OK=$'\033[32m' C_FAIL=$'\033[31m' C_SKIP=$'\033[33m'
    C_DIM=$'\033[2m' C_BOLD=$'\033[1m' C_RST=$'\033[0m'
else
    C_OK="" C_FAIL="" C_SKIP="" C_DIM="" C_BOLD="" C_RST=""
fi

_ts() { date '+%H:%M:%S.%3N'; }
_ms() { date '+%s%3N'; }
_fmt_ms() { awk -v ms="$1" 'BEGIN { printf "%.2f с", ms / 1000 }'; }
_log() { local level="$1"; shift; printf '%s %-6s %s\n' "$(_ts)" "$level" "$*" >> "$LOG_FILE"; }
_say() { local color="$1" level="$2"; shift 2; printf '%s%s%s\n' "$color" "$*" "$C_RST"; _log "$level" "$*"; }
_log_block() {
    local title="$1" text="$2"
    if [ -z "$text" ]; then _log DATA "$title: <пусто>"; return; fi
    _log DATA "$title:"
    printf '%s\n' "$text" | sed 's/^/                    | /' >> "$LOG_FILE"
}
_cmdstr() { local s; printf -v s '%q ' "$@"; printf '%s' "${s% }"; }
_quote() { printf "'%s'" "$1"; }

SUITE_START="$(_ms)"
CASE_ID=""
RESULTS=()

suite() {
    _say "$C_BOLD" SUITE "══ $SUITE_NAME: $1 ══"
    _log INFO "программы: $BIN_DIR, типы: $TYPES"
    _log INFO "система: $(uname -sr), пользователь: $(id -un), QT_QPA_PLATFORM=$QT_QPA_PLATFORM"
    [ -z "${TEST_CASE:-}" ] || _log INFO "фильтр кейсов: $TEST_CASE"
}

for_each_type() {
    local id="$1" desc="$2" fn="$3" type
    for type in $TYPES; do
        run_case "$id-$type" "[$type] $desc" "$fn" "$type"
    done
}

run_case() {
    local id="$1" desc="$2" fn="$3"
    shift 3
    if [ -n "${TEST_CASE:-}" ] && [[ " $TEST_CASE " != *" $id "* ]]; then
        _log INFO "кейс $id пропущен фильтром TEST_CASE"
        return
    fi
    _case_begin "$id" "$desc"
    "$fn" "$@"
    _case_end
}

_case_begin() {
    CASE_ID="$1"
    CASE_DESC="$2"
    CASE_START="$(_ms)"
    CASE_CHECKS=0
    CASE_FAILS=0
    HOST_PIDS=()
    HOST_PID=""
    CASE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/wolf-test-$CASE_ID.XXXXXX")"
    printf '\n' >> "$LOG_FILE"
    _say "$C_BOLD" CASE "▶ $CASE_ID  $CASE_DESC"
    _log INFO "каталог кейса: $CASE_DIR"
}

_case_end() {
    [ -n "$CASE_ID" ] || return 0
    local status=PASS color="$C_OK"
    if [ "$CASE_FAILS" -gt 0 ]; then
        status=FAIL
        color="$C_FAIL"
        _collect_artifacts
    fi
    _stop_processes
    local ms=$(( $(_ms) - CASE_START ))
    _say "$color" RESULT "  $status  $CASE_ID  (проверок: $CASE_CHECKS, $(_fmt_ms "$ms"))"
    RESULTS+=("$CASE_ID|$status|$ms|$CASE_DESC")
    [ "$status" = FAIL ] || rm -rf "$CASE_DIR"
    CASE_ID=""
}

_stop_processes() {
    local pid child
    for pid in "${HOST_PIDS[@]}"; do
        for child in $(pgrep -P "$pid" 2>/dev/null); do kill_quiet KILL "$child"; done
    done
    for pid in "${HOST_PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -TERM "$pid" 2>/dev/null
            _poll 8 _process_gone "$pid" || kill_quiet KILL "$pid"
        fi
        { wait "$pid"; } 2>/dev/null
        rm -rf "/tmp/wolf_$pid"
    done
}

_collect_artifacts() {
    local dst="$ARTIFACT_DIR/$CASE_ID" pid
    mkdir -p "$dst"
    cp -a "$CASE_DIR/." "$dst/" 2>/dev/null
    {
        echo "== процессы host_*"
        ps -o pid,ppid,stat,etime,args -C "host_pipe,host_fifo,host_sock" 2>&1
        for pid in "${HOST_PIDS[@]}"; do
            echo
            echo "== потомки хоста $pid"
            pgrep -a -P "$pid" 2>&1 || true
            echo "== файлы /tmp/wolf_$pid"
            ls -la "/tmp/wolf_$pid" 2>&1
        done
    } > "$dst/processes.txt"
    local f
    for f in "$CASE_DIR"/*.log "$CASE_DIR"/*.out; do
        [ -e "$f" ] && _log_block "$(basename "$f")" "$(cat "$f")"
    done
    _log_block "processes.txt" "$(cat "$dst/processes.txt")"
    _say "$C_FAIL" INFO "    артефакты: $dst"
}

finish() {
    _case_end
    local total=0 passed=0 failed=0 row id status ms desc failed_ids="" line color
    printf '\n' >> "$LOG_FILE"
    _say "$C_BOLD" SUITE "── итог $SUITE_NAME ──"
    for row in "${RESULTS[@]}"; do
        IFS='|' read -r id status ms desc <<< "$row"
        total=$((total + 1))
        color="$C_OK"
        if [ "$status" = FAIL ]; then
            failed=$((failed + 1)); failed_ids="$failed_ids $id"; color="$C_FAIL"
        else
            passed=$((passed + 1))
        fi
        printf -v line '  %-9s %-4s %8s  %s' "$id" "$status" "$(_fmt_ms "$ms")" "$desc"
        _say "$color" SUMMARY "$line"
    done
    color="$C_OK"
    [ "$failed" -eq 0 ] || color="$C_FAIL"
    _say "$color" SUMMARY "кейсов: $total, PASS: $passed, FAIL: $failed, время: $(_fmt_ms $(( $(_ms) - SUITE_START )))"
    if [ "$failed" -gt 0 ]; then
        _say "$C_FAIL" SUMMARY "упали:$failed_ids"
        _say "$C_DIM" SUMMARY "повторить: TEST_CASE=\"${failed_ids# }\" bash $0 $BIN_DIR"
    fi
    _say "$C_DIM" SUMMARY "подробный лог: $LOG_FILE"
    trap - EXIT
    [ "$total" -gt 0 ] || exit 1
    exit $(( failed > 0 ))
}

_on_interrupt() {
    trap - INT TERM
    [ -z "$CASE_ID" ] || _fail "кейс прерван сигналом"
    finish
}
trap _on_interrupt INT TERM
trap '[ -z "$CASE_ID" ] || { _fail "набор завершился посреди кейса"; _case_end; }' EXIT

step() { _say "$C_DIM" STEP "    · $*"; }
_pass() { CASE_CHECKS=$((CASE_CHECKS + 1)); _say "$C_OK" PASS "    ✓ $1"; }
_fail() {
    local what="$1" d
    shift
    CASE_CHECKS=$((CASE_CHECKS + 1))
    CASE_FAILS=$((CASE_FAILS + 1))
    _say "$C_FAIL" FAIL "    ✗ $what"
    for d in "$@"; do _say "$C_FAIL" FAIL "        $d"; done
}

expect_eq() {
    if [ "$2" = "$3" ]; then _pass "$1"; else _fail "$1" "ожидалось: $(_quote "$3")" "получено:  $(_quote "$2")"; fi
}
expect_ne() {
    if [ "$2" != "$3" ]; then _pass "$1"; else _fail "$1" "значение не должно быть $(_quote "$3")"; fi
}
expect_true() {
    local what="$1"; shift
    if "$@"; then _pass "$what"; else _fail "$what" "условие ложно: $(_cmdstr "$@")"; fi
}
expect_false() {
    local what="$1"; shift
    if ! "$@"; then _pass "$what"; else _fail "$what" "условие должно быть ложным: $(_cmdstr "$@")"; fi
}
expect_contains() {
    if [[ "$2" == *"$3"* ]]; then _pass "$1"
    else _fail "$1" "ожидалась подстрока: $(_quote "$3")" "в тексте:           $(_quote "${2:0:600}")"; fi
}
expect_between() {
    if [ "$2" -ge "$3" ] && [ "$2" -le "$4" ]; then _pass "$1 ($2)"
    else _fail "$1" "ожидалось от $3 до $4" "получено:  $2"; fi
}

_poll() {
    local timeout="$1"; shift
    local deadline=$(( $(_ms) + timeout * 1000 ))
    until "$@"; do
        [ "$(_ms)" -lt "$deadline" ] || return 1
        sleep 0.1
    done
}

wait_until() {
    local what="$1" timeout="$2"; shift 2
    local start; start="$(_ms)"
    if _poll "$timeout" "$@"; then
        _pass "$what (через $(_fmt_ms $(( $(_ms) - start ))))"
        return 0
    fi
    _fail "$what" "не выполнилось за $timeout с: $(_cmdstr "$@")"
    return 1
}

start_host() {
    local type="$1"; shift
    step "запуск: host_$type $*"
    "$BIN_DIR/host_$type" "$@" > "$CASE_DIR/host.out" 2> "$CASE_DIR/host.log" &
    HOST_PID=$!
    HOST_PIDS+=("$HOST_PID")
    HOST_LOG="$CASE_DIR/host.log"
    if ! _poll 10 host_log_has "хост запущен"; then
        _fail "хост запустился" "за 10 с в журнале нет «хост запущен»" "журнал: $(_quote "$(cat "$HOST_LOG")")"
        return 1
    fi
    _log INFO "хост запущен, pid $HOST_PID"
}

child_pids() { pgrep -P "$HOST_PID" 2>/dev/null | sort -n; }
child_count() { child_pids | wc -l; }
first_child_pid() { child_pids | head -1; }
child_count_is() { [ "$(child_count)" -eq "$1" ]; }

_process_gone() {
    local state
    state="$(sed -E 's/^.*\) //' "/proc/$1/stat" 2>/dev/null | cut -d' ' -f1)"
    [ -z "$state" ] || [ "$state" = Z ]
}
is_running() { ! _process_gone "$1"; }

kill_quiet() {
    { kill "-$1" "$2"; wait "$2"; } 2>/dev/null
    return 0
}

host_log_has() { grep -q -- "$1" "$HOST_LOG" 2>/dev/null; }
file_has() { grep -q -- "$2" "$1" 2>/dev/null; }
count_in() { grep -c -- "$2" "$1" 2>/dev/null || true; }
host_log_count_is() { [ "$(count_in "$HOST_LOG" "$1")" -eq "$2" ]; }

round_lines() { grep -E 'INFO : раунд [0-9]+:' "$HOST_LOG" 2>/dev/null; }
round_count() { round_lines | wc -l; }
rounds_at_least() { [ "$(round_count)" -ge "$1" ]; }

runtime_dir_exists() { [ -e "/tmp/wolf_$1" ]; }

validate_rounds() {
    LC_ALL=C awk -f "$PROJECT_DIR/tests/integration/validate_rounds.awk" "$HOST_LOG"
}
