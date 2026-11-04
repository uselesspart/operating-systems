#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SOURCE="${ROOT_DIR}/config/rsyslog/30-disk-monitor.conf"
DESTINATION="/etc/rsyslog.d/30-disk-monitor.conf"
DEST_DIR="$(dirname -- "$DESTINATION")"
BACKUP=""
HAD_OLD=0

INSTALLED=0
COMMITTED=0

rollback() {
  if [[ "$INSTALLED" -eq 1 ]]; then
    if [[ "$HAD_OLD" -eq 1 && -n "$BACKUP" && -e "$BACKUP" ]]; then
      cp -a -- "$BACKUP" "$DESTINATION"
    else
      rm -f -- "$DESTINATION"
    fi
    INSTALLED=0
  fi
  if [[ -n "$BACKUP" && -e "$BACKUP" ]]; then
    rm -f -- "$BACKUP"
  fi
  BACKUP=""
}

cleanup() {
  if [[ "$COMMITTED" -ne 1 ]]; then
    rollback
  elif [[ -n "$BACKUP" && -e "$BACKUP" ]]; then
    rm -f -- "$BACKUP"
  fi
}
trap cleanup EXIT

if [[ "${EUID}" -ne 0 ]]; then
  echo "Run this script with sudo: sudo $0" >&2
  exit 1
fi
if ! command -v rsyslogd >/dev/null 2>&1; then
  echo "rsyslogd is not installed. Install rsyslog first." >&2
  exit 1
fi
if [[ ! -d "$DEST_DIR" ]]; then
  echo "rsyslog configuration directory does not exist: $DEST_DIR" >&2
  exit 1
fi

BACKUP="$(mktemp "${DEST_DIR}/.30-disk-monitor.conf.backup.XXXXXX")"
if [[ -e "$DESTINATION" ]]; then
  HAD_OLD=1
  cp -a -- "$DESTINATION" "$BACKUP"
else
  HAD_OLD=0
  rm -f -- "$BACKUP"
fi

INSTALLED=1
if ! install -o root -g root -m 0644 "$SOURCE" "$DESTINATION"; then
  echo "Could not install $DESTINATION; restoring the previous state." >&2
  rollback
  exit 1
fi
if ! rsyslogd -N1; then
  echo "rsyslog validation failed; restoring the previous $DESTINATION." >&2
  rollback
  exit 1
fi

activated=0
if command -v systemctl >/dev/null 2>&1; then
  if systemctl is-active --quiet rsyslog; then
    if systemctl reload rsyslog || systemctl restart rsyslog; then
      activated=1
    fi
  elif systemctl start rsyslog; then
    activated=1
  fi
fi
if [[ "$activated" -ne 1 ]] && command -v service >/dev/null 2>&1; then
  if service rsyslog restart; then
    activated=1
  fi
fi

if [[ "$activated" -ne 1 ]]; then
  echo "Validated config could not be activated (no supported running/startable rsyslog service). Restoring previous file." >&2
  rollback
  exit 1
fi

rm -f -- "$BACKUP"
BACKUP=""
INSTALLED=0
COMMITTED=1
echo "Installed, validated, and activated $DESTINATION"