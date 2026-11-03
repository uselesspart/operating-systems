#!/usr/bin/env bash
# Общие функции интеграционных тестов чата
#
# Проверяются настоящие программы host_<тип> и client_<тип> из CHAT_BIN_DIR
# (по умолчанию каталог проекта, где их собирает make). Хост запускается без экрана
# (QT_QPA_PLATFORM=offscreen), клиенты читают ввод из именованных каналов.
# Каждый тест работает в своём временном каталоге /tmp/chat_test.XXXXXX

set -u

LAB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BIN_DIR="${CHAT_BIN_DIR:-$LAB_DIR}"
TIMEOUT="${CHAT_TEST_TIMEOUT:-5}"
QUIET_PERIOD="${CHAT_TEST_QUIET:-1}"

PASSED=0
FAILED=0
TEST_ROOT=""
TYPE=""
HOST_PID=""
HOST_NAME=host
HOST_ENV=()
HOST_PIDS=()
SPAWNED_PID=""
declare -A CLIENT_PID=()
declare -A CLIENT_FD=()

die()
{
    echo "Ошибка: $*" >&2
    exit 2
}

ok()
{
    PASSED=$((PASSED + 1))
    printf '  ok    %s\n' "$1"
}

fail()
{
    FAILED=$((FAILED + 1))
    printf '  FAIL  %s\n' "$1"
}

check()
{
    local description=$1
    shift
    if "$@"; then
        ok "$description"
    else
        fail "$description"
    fi
}

not() { ! "$@"; }
equals() { [ "$1" = "$2" ]; }
file_has() { grep -Fq -- "$2" "$1" 2>/dev/null; }
now_ms() { echo $(($(date +%s%N) / 1000000)); }
between() { [ "$1" -ge "$2" ] && [ "$1" -le "$3" ]; }

is_running()
{
    local stat
    stat=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    stat=${stat##*) }
    [ "${stat%% *}" != Z ]
}

wait_exit()
{
    local pid=$1 timeout=${2:-$TIMEOUT}
    local deadline=$(($(now_ms) + timeout * 1000))
    while is_running "$pid"; do
        [ "$(now_ms)" -ge "$deadline" ] && return 1
        sleep 0.1
    done
}

wait_file_has()
{
    local file=$1 text=$2 timeout=${3:-$TIMEOUT}
    local deadline=$(($(now_ms) + timeout * 1000))
    until file_has "$file" "$text"; do
        [ "$(now_ms)" -ge "$deadline" ] && return 1
        sleep 0.1
    done
}

show_tail()
{
    tail -n 5 "$1" 2>/dev/null | sed 's/^/        | /'
}

conn_types()
{
    local host
    for host in "$BIN_DIR"/host_*; do
        [ -x "$host" ] && echo "${host##*/host_}"
    done
}

host_log() { echo "$TEST_ROOT/$HOST_NAME.log"; }
output_of() { echo "$TEST_ROOT/$1.out"; }
status_of() { cat "$TEST_ROOT/$1.status" 2>/dev/null; }

# Запускает программу в фоне: spawn ИМЯ команда... [перенаправления]
# pid программы попадает в SPAWNED_PID, код завершения в файл ИМЯ.status. Программа работает
# внутри подоболочки без stderr, поэтому сообщения bash об убитых процессах не засоряют вывод.
# Программе закрываются каналы ввода клиентов, чтобы закрытие канала тестом было концом ввода.
# Без явного 0<&0 фоновая команда получила бы ввод из /dev/null
spawn()
{
    local name=$1 fd
    shift
    rm -f "$TEST_ROOT/$name.pid" "$TEST_ROOT/$name.status"
    (
        for fd in ${CLIENT_FD[@]+"${CLIENT_FD[@]}"}; do
            eval "exec $fd>&-"
        done
        exec 3>&2 2>/dev/null
        "$@" 0<&0 2>&3 3>&- &
        echo $! >"$TEST_ROOT/$name.pid"
        wait $!
        echo $? >"$TEST_ROOT/$name.status"
    ) 0<&0 &
    until [ -s "$TEST_ROOT/$name.pid" ]; do
        sleep 0.01
    done
    SPAWNED_PID=$(cat "$TEST_ROOT/$name.pid")
}

wait_status()
{
    local file="$TEST_ROOT/$1.status" timeout=${2:-$TIMEOUT}
    local deadline=$(($(now_ms) + timeout * 1000))
    until [ -s "$file" ]; do
        [ "$(now_ms)" -ge "$deadline" ] && return 1
        sleep 0.05
    done
}

# Хост; переменные окружения для него можно задать в HOST_ENV=(ИМЯ=значение ...)
start_host()
{
    HOST_NAME="host$((${#HOST_PIDS[@]} + 1))"
    spawn "$HOST_NAME" env ${HOST_ENV[@]+"${HOST_ENV[@]}"} QT_QPA_PLATFORM=offscreen "$BIN_DIR/host_$TYPE" \
        >"$(host_log)" 2>&1
    HOST_PID=$SPAWNED_PID
    HOST_PIDS+=("$HOST_PID")
    wait_file_has "$(host_log)" "Host started: pid $HOST_PID, connection type $TYPE"
}

require_host()
{
    start_host && return 0
    fail "хост не запустился"
    show_tail "$(host_log)"
    return 1
}

# Клиент: start_client ИМЯ [аргументы вместо "pid хоста, имя"]. Ввод идёт через именованный
# канал; тест держит его открытым (<> не ждёт читателя), пока не вызовет close_input
start_client()
{
    local name=$1 fd
    shift
    local input="$TEST_ROOT/$name.in" args=("$HOST_PID" "$name")
    [ "$#" -gt 0 ] && args=("$@")
    mkfifo "$input"
    exec {fd}<>"$input"
    CLIENT_FD[$name]=$fd
    spawn "$name" "$BIN_DIR/client_$TYPE" "${args[@]}" <"$input" >"$(output_of "$name")" 2>&1
    CLIENT_PID[$name]=$SPAWNED_PID
}

join_chat()
{
    local name=$1
    start_client "$name"
    wait_file_has "$(output_of "$name")" "Подключено к хосту $HOST_PID ($TYPE)" && return 0
    fail "$name не подключился"
    show_tail "$(output_of "$name")"
    return 1
}

say() { printf '%s\n' "$2" >&"${CLIENT_FD[$1]}"; }

close_input()
{
    local fd=${CLIENT_FD[$1]}
    eval "exec $fd>&-"
    unset "CLIENT_FD[$1]"
}

expect_output()
{
    local description=$1 name=$2 text=$3 timeout=${4:-$TIMEOUT}
    if wait_file_has "$(output_of "$name")" "$text" "$timeout"; then
        ok "$description"
    else
        fail "$description"
        show_tail "$(output_of "$name")"
    fi
}

expect_no_output()
{
    local description=$1 name=$2 text=$3
    sleep "$QUIET_PERIOD"
    check "$description" not file_has "$(output_of "$name")" "$text"
}

expect_host_log()
{
    local description=$1 text=$2 timeout=${3:-$TIMEOUT}
    if wait_file_has "$(host_log)" "$text" "$timeout"; then
        ok "$description"
    else
        fail "$description"
        show_tail "$(host_log)"
    fi
}

# expect_exit ОПИСАНИЕ ИМЯ КОД [ТАЙМ-АУТ]; убитый сигналом N процесс даёт код 128 + N
expect_exit()
{
    local description=$1 name=$2 code=$3 timeout=${4:-$TIMEOUT}
    if ! wait_status "$name" "$timeout"; then
        fail "$description: процесс не завершился за $timeout с"
        return
    fi
    if [ "$(status_of "$name")" = "$code" ]; then
        ok "$description"
    else
        fail "$description: код $(status_of "$name") вместо $code"
        show_tail "$(output_of "$name")"
    fi
}

leftovers_of() { ls -d "/tmp/chat_$1" /dev/shm/sem.chat_"$1"_* 2>/dev/null; }

remove_leftovers()
{
    rm -rf "/tmp/chat_$1" /dev/shm/sem.chat_"$1"_* 2>/dev/null
}

setup_test()
{
    TEST_ROOT=$(mktemp -d /tmp/chat_test.XXXXXX)
    HOST_PID=""
    HOST_NAME=host
    HOST_ENV=()
    HOST_PIDS=()
    CLIENT_PID=()
    CLIENT_FD=()
}

teardown_test()
{
    local name pid
    for name in "${!CLIENT_FD[@]}"; do
        close_input "$name"
    done
    for pid in ${CLIENT_PID[@]+"${CLIENT_PID[@]}"}; do
        kill -KILL "$pid" 2>/dev/null
    done
    for pid in ${HOST_PIDS[@]+"${HOST_PIDS[@]}"}; do
        if is_running "$pid"; then
            kill -TERM "$pid" 2>/dev/null
            wait_exit "$pid" || kill -KILL "$pid" 2>/dev/null
        fi
    done
    wait
    for pid in ${HOST_PIDS[@]+"${HOST_PIDS[@]}"}; do
        remove_leftovers "$pid"
    done
    [ -n "$TEST_ROOT" ] && rm -rf "$TEST_ROOT"
    TEST_ROOT=""
    CLIENT_PID=()
    HOST_PIDS=()
}

run_test()
{
    printf '\n%s%s\n' "$2" "${TYPE:+ [$TYPE]}"
    setup_test
    "$1"
    teardown_test
}

# Прогоняет набор тестов для каждого типа связи (или для CHAT_TEST_TYPES="sock fifo")
run_for_each_type()
{
    local types
    types=${CHAT_TEST_TYPES:-$(conn_types)}
    [ -n "$types" ] || die "в $BIN_DIR нет программ host_*, соберите проект: make"
    for TYPE in $types; do
        [ -x "$BIN_DIR/client_$TYPE" ] || die "нет $BIN_DIR/client_$TYPE"
        "$@"
    done
}

init_suite()
{
    printf '== %s (%s)\n' "$1" "$(basename "$0")"
    trap 'teardown_test; exit 130' INT TERM

    [ "${2:-}" = build ] && return 0
    if [ -z "$(conn_types)" ] && [ "$BIN_DIR" = "$LAB_DIR" ]; then
        (cd "$LAB_DIR" && sh build.sh >/dev/null) || die "не удалось собрать проект"
    fi
}

finish()
{
    printf '\nИтог: %d ok, %d FAIL\n' "$PASSED" "$FAILED"
    [ "$FAILED" -eq 0 ]
}
