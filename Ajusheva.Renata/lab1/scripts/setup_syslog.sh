#!/bin/sh
# Настройка «своего» LOG_LOCAL0: сообщения diskmond с facility local0 -> /var/log/diskmond.log.
# Копирует правило syslog/20-diskmond.conf в /etc/rsyslog.d/, проверяет конфигурацию rsyslog,
# перезапускает его и убеждается, что тестовое сообщение дошло до отдельного журнала.
#
# Использование:  sudo sh scripts/setup_syslog.sh            установить правило
#                 sudo sh scripts/setup_syslog.sh --remove   убрать правило

set -eu

LAB_DIR=$(cd "$(dirname "$0")/.." && pwd)
RULE_SOURCE="$LAB_DIR/syslog/20-diskmond.conf"
RULE_TARGET=/etc/rsyslog.d/20-diskmond.conf
LOG_FILE=/var/log/diskmond.log

fail()
{
    echo "setup_syslog.sh: $*" >&2
    exit 1
}

restart_rsyslog()
{
    if [ -d /run/systemd/system ] && command -v systemctl >/dev/null 2>&1; then
        systemctl restart rsyslog
        return
    fi
    if command -v service >/dev/null 2>&1 && service rsyslog restart >/dev/null 2>&1; then
        return
    fi
    
    if pgrep -x rsyslogd >/dev/null 2>&1; then
        pkill -x rsyslogd
        tries=0
        while pgrep -x rsyslogd >/dev/null 2>&1 && [ "$tries" -lt 50 ]; do
            sleep 0.1
            tries=$((tries + 1))
        done
    fi
    rm -f /run/rsyslogd.pid
    rsyslogd
}

[ "$(id -u)" -eq 0 ] || fail "run as root: sudo sh scripts/setup_syslog.sh"
command -v rsyslogd >/dev/null 2>&1 || fail "rsyslog is not installed (Debian/Ubuntu: sudo apt install rsyslog)"

if [ "${1:-}" = "--remove" ]; then
    rm -f "$RULE_TARGET"
    restart_rsyslog
    echo "Rule $RULE_TARGET removed ($LOG_FILE is kept)"
    exit 0
fi

cp "$RULE_SOURCE" "$RULE_TARGET"
chmod 644 "$RULE_TARGET"
if ! rsyslogd -N1 >/dev/null 2>&1; then
    rsyslogd -N1 >&2 || true
    rm -f "$RULE_TARGET"
    fail "rsyslog rejected the configuration, the rule was not installed"
fi
restart_rsyslog
echo "Installed $RULE_TARGET, rsyslog restarted"


marker="rsyslog rule check $(date +%s).$$"
logger -p local0.info -t diskmond "$marker"
tries=0
while [ "$tries" -lt 50 ]; do
    if grep -qF "$marker" "$LOG_FILE" 2>/dev/null; then
        echo "OK: LOG_LOCAL0 messages of diskmond are written to $LOG_FILE"
        exit 0
    fi
    sleep 0.1
    tries=$((tries + 1))
done
fail "the test message did not reach $LOG_FILE, check that rsyslog is running"
