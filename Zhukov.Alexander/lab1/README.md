# Лабораторная работа 1. Disk monitor — вариант №20

## Настройка журнала

В Ubuntu, из папки проекта:

```bash
sudo install -m 644 rsyslog/os-lab1.conf /etc/rsyslog.d/30-os-lab1.conf
sudo rsyslogd -N1 && sudo systemctl restart rsyslog
logger -p local0.info -t disk_monitor "Log check"
```

Файловый журнал создаётся с правами `syslog:adm`, `0640`.
Для чтения журналов и запуска тестов пользователь должен входить в группу `adm`.

## Сборка и запуск

```bash
./build.sh
./bin/disk_monitor --check-config ./config.yaml
./bin/disk_monitor ./config.yaml
```

После `Monitoring started` программа работает в фоне. Повторный запуск заменяет
предыдущий экземпляр.

Каталоги задаются списком `paths` в YAML. Относительные пути считаются от папки
конфига, вложенные каталоги добавляются отдельно.
Файлы журналов должны находиться вне наблюдаемых каталогов.

Можно создать файл и посмотреть события:

```bash
echo test > examples/first/test.txt
tail -f /var/log/os_lab1.log
```

## Перечитывание конфигурации

После изменения YAML:

```bash
read -r pid _ < "$HOME/.local/state/os-lab1/disk_monitor.pid"
kill -HUP "$pid"
```

PID сохраняется. Если новый конфиг неверный, демон продолжает наблюдать прежние
каталоги, а причину записывает в `/var/log/syslog`.

## Остановка

```bash
read -r pid _ < "$HOME/.local/state/os-lab1/disk_monitor.pid"
kill -TERM "$pid"
```

После завершения PID-файл удаляется.

## Журналы

```bash
tail -f /var/log/os_lab1.log
tail -f /var/log/syslog
```

## Тесты

```bash
./tests/run.sh
```
