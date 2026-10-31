#!/bin/bash
# Добавляет событие в конфигурационный файл напоминалки.
#
# Использование: ./add_event.sh ДАТА ВРЕМЯ [-h|-d|-w] ТЕКСТ
#   ДАТА   - ГГГГ-ММ-ДД
#   ВРЕМЯ  - ЧЧ:ММ или ЧЧ:ММ:СС
#   флаг   - необязательный: -h каждый час, -d каждый день, -w каждую неделю
#
# Переменные окружения (необязательные):
#   REMINDER_CONFIG   - путь к конфигу (по умолчанию reminder.conf рядом со скриптом)
#   REMINDERD_PIDFILE - путь к pid-файлу демона
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG="${REMINDER_CONFIG:-$ROOT/reminder.conf}"
PIDFILE="${REMINDERD_PIDFILE:-/tmp/reminderd_$(id -u).pid}"

usage() {
    echo "Использование: $0 ДАТА(ГГГГ-ММ-ДД) ВРЕМЯ(ЧЧ:ММ[:СС]) [-h|-d|-w] ТЕКСТ" >&2
    exit 1
}
die() { echo "Ошибка: $1" >&2; exit 1; }

[ $# -ge 3 ] || usage
date_s="$1"
time_s="$2"
shift 2

flag=""
case "$1" in
    -h|-d|-w) flag="$1"; shift ;;
esac
text="$*"
text="${text//$'\n'/ }"
[ -n "${text// /}" ] || usage

[[ "$date_s" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || die "дата должна быть в формате ГГГГ-ММ-ДД"
[ "$(date -d "$date_s" +%F 2>/dev/null)" = "$date_s" ] || die "несуществующая дата: $date_s"
[[ "$time_s" =~ ^[0-9]{2}:[0-9]{2}(:[0-9]{2})?$ ]] || die "время должно быть в формате ЧЧ:ММ или ЧЧ:ММ:СС"
date -d "$time_s" >/dev/null 2>&1 || die "несуществующее время: $time_s"

echo "$date_s $time_s ${flag:+$flag }$text" >> "$CONFIG" || die "не удалось записать в $CONFIG"
echo "Событие добавлено в $CONFIG"

# Сообщаем демону (если он запущен), чтобы он перечитал конфиг
if [ -r "$PIDFILE" ]; then
    pid="$(<"$PIDFILE")"
    if [[ "$pid" =~ ^[0-9]+$ ]] && [ "$(cat "/proc/$pid/comm" 2>/dev/null)" = "reminderd" ]; then
        kill -HUP "$pid" && echo "Демон (pid $pid) уведомлён (SIGHUP)"
        exit 0
    fi
fi
echo "Демон не запущен: события будут прочитаны при его старте."
