#!/usr/bin/env bash
set -euo pipefail
base=$(cd -- "$(dirname -- "$0")" && pwd)
config=${REMINDER_CONFIG:-reminder.conf}
pidfile=${REMINDER_PIDFILE:-$config.pid}
if (( $# < 3 )); then
    printf 'Usage: %s YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] text\n' "$0" >&2
    exit 1
fi
event="add_event $1 $2"
shift 2
if [[ ${1:-} == -h || ${1:-} == -d || ${1:-} == -w ]]; then
    event+=" $1"
    shift
fi
(( $# > 0 )) || { printf 'Missing reminder text\n' >&2; exit 1; }
text="$*"
[[ $text != *$'\n'* && $text != *$'\r'* ]] || { printf 'Text must occupy one line\n' >&2; exit 1; }
event+=" $text"
"$base/reminder" --check-event "$event"
# Advisory lock serializes concurrent writers. An extra newline handles a config
# whose previous last line has no terminating newline.
exec 9>>"$config"
flock -x 9
printf '\n%s\n' "$event" >&9
flock -u 9
printf 'Added: %s\n' "$event"
if [[ -r $pidfile ]]; then
    read -r pid < "$pidfile" || pid=''
    if [[ $pid =~ ^[0-9]+$ && $pid -gt 1 && -d /proc/$pid ]]; then
        executable=$(readlink -- "/proc/$pid/exe" || true)
        expected=$(readlink -f -- "$base/reminder")
        if [[ $executable == "$expected" && -O /proc/$pid ]]; then
            kill -HUP "$pid" || printf 'Event saved; cannot signal daemon\n' >&2
        fi
    fi
fi
