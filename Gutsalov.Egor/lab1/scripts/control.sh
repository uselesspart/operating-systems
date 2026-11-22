#!/usr/bin/env bash
set -euo pipefail

lab_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
if (( $# < 1 || $# > 3 )); then
    echo "Usage: $0 {start|restart|stop|reload|status} [CONFIG] [PID_FILE]" >&2
    exit 2
fi
action=$1
config=${2:-"$lab_dir/lab1.conf"}
pid_file=${3:-"/tmp/gutsalov-lab1-$(id -u).pid"}

running_pid() {
    local pid executable
    [[ -f "$pid_file" ]] || return 1
    read -r pid < "$pid_file" || return 1
    [[ "$pid" =~ ^[0-9]+$ && ${#pid} -le 10 ]] || return 1
    (( pid > 1 )) || return 1
    executable=$(readlink -- "/proc/$pid/exe") || return 1
    executable=${executable% (deleted)}
    [[ "$executable" == "$lab_dir/lab1" ]] || return 1
    printf '%s\n' "$pid"
}

case "$action" in
    start|restart)
        "$lab_dir/lab1" "$config" --pid-file "$pid_file"
        ;;
    status)
        if pid=$(running_pid); then
            printf 'Running, PID %s\n' "$pid"
        else
            echo 'Stopped'
            exit 1
        fi
        ;;
    reload|stop)
        if ! pid=$(running_pid); then
            echo 'Daemon is not running' >&2
            exit 1
        fi
        if [[ "$action" == reload ]]; then
            kill -HUP "$pid"
        else
            kill -TERM "$pid"
            for (( attempt=0; attempt<100; ++attempt )); do
                current_pid=$(running_pid) || exit 0
                [[ "$current_pid" == "$pid" ]] || exit 0
                sleep 0.1
            done
            echo 'Timed out waiting for daemon shutdown' >&2
            exit 1
        fi
        ;;
    *)
        echo "Unknown action: $action" >&2
        exit 2
        ;;
esac
