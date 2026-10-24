#!/usr/bin/env bash
set -eu

LOGFILE=/var/log/lab1_disk_monitor.log
CONF=/etc/rsyslog.d/30-lab1-disk-monitor.conf

if [ "$(id -u)" -ne 0 ]; then
    exec sudo bash "$0" "$@"
fi

if ! command -v rsyslogd >/dev/null 2>&1; then
    apt-get update
    apt-get install -y rsyslog
fi

cat > "$CONF" << 'CONF_EOF'
# Disk monitor (лаба 1, вариант 20): все сообщения фасилити LOG_LOCAL0
# пишутся в отдельный файл и дальше в общий syslog не попадают.
local0.*    /var/log/lab1_disk_monitor.log
& stop
CONF_EOF

touch "$LOGFILE"
chown syslog:adm "$LOGFILE" 2>/dev/null || true
chmod 640 "$LOGFILE"

if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
    systemctl restart rsyslog
else
    service rsyslog restart
fi

logger -p local0.info -t lab1_daemon "setup_syslog: LOG_LOCAL0 routing check"
sleep 1
echo "Готово. Журнал событий: $LOGFILE"
echo "Смотреть: tail -f $LOGFILE  (если нет прав: sudo tail -f $LOGFILE)"
