# Лабораторная работа №1. Демон Disk monitor (вариант 20)

Демон следит через `inotify` за каталогами, указанными в конфигурационном файле,
и записывает каждое обращение к файлам (открытие, чтение, запись, создание,
удаление, переименование, смена атрибутов) в отдельный log-файл. События
пишутся в syslog с facility `LOG_LOCAL0`, а правило rsyslog направляет их в
`/var/log/disk_monitor.log`.

## Source

```
lab1/
├── build.sh                      builder
├── disk_monitor.conf             configuration
├── rsyslog/
│   └── 30-disk_monitor.conf      rule rsyslog for LOG_LOCAL0
├── include/                      .h
│   ├── DiskMonitor.h             singleton class
│   ├── FileWatcher.h             recursion inotify
│   ├── Config.h                  config
│   ├── PidFile.h                 pid file
│   ├── Logger.h                  wrapper openlog/syslog/closelog
│   ├── PathUtils.h               path
│   └── UniqueFd.h                RAII-owner file descriptor
└── src/                          .cpp
    ├── main.cpp                  main
    ├── DiskMonitor.cpp           daemon, life loop, protect
    ├── FileWatcher.cpp           inotify, log recording
    ├── Config.cpp
    ├── PidFile.cpp
    ├── Logger.cpp
    └── PathUtils.cpp
```

## Сборка

```sh
./build.sh          # sh build.sh
```

## Настройка отдельного log-файла (LOG_LOCAL0)

Один раз:

```sh
sudo cp rsyslog/30-disk_monitor.conf /etc/rsyslog.d/
sudo systemctl restart rsyslog
```

Правило отправляет все сообщения `local0` в `/var/log/disk_monitor.log` и не
дублирует их в `/var/log/syslog`. Служебные сообщения демона идут с facility `daemon` в обычный
системный журнал.

## Конфигурационный файл

По умолчанию демон берёт `disk_monitor.conf` из текущего (рабочего) каталога
и запоминает его абсолютный путь, поэтому SIGHUP работает и после того, как
демон перешёл в `/`. Другой файл можно передать аргументом:
`./disk_monitor /path/to/file.conf`.

Формат: одна строка - один каталог.

```
# каталоги для наблюдения
/tmp/dm_test/dir1
/tmp/dm_test/dir2
```

- подкаталоги отслеживаются рекурсивно
- относительные пути отсчитываются от каталога, где лежит конфиг
- несуществующие каталоги пропускаются с предупреждением
- повторы и каталоги, вложенные в уже указанные, отбрасываются
- не указывайте `/var/log`: демон начнёт реагировать на запись собственного журнала

## Запуск и управление

```sh
mkdir -p /tmp/dm_test/dir1 /tmp/dm_test/dir2
sudo ./disk_monitor
```

Команда сразу возвращает управление терминалу, демон продолжает работать в фоне.

pid-файл: `/var/run/disk_monitor.pid` при запуске от root, `/tmp/disk_monitor.pid`
при запуске от обычного пользователя. 

| Действие | Команда |
|---|---|
| перечитать конфиг | `sudo kill -HUP $(cat /var/run/disk_monitor.pid)` |
| остановить | `sudo kill -TERM $(cat /var/run/disk_monitor.pid)` |
| перезапустить | повторно выполнить `sudo ./disk_monitor` |
| проверить, что работает | `ps -o pid,ppid,sid,tty,stat,cmd -C disk_monitor` |

## Журналы

| Что | Где |
|---|---|
| события файлов (`local0`) | `sudo tail -f /var/log/disk_monitor.log` |
| запуск, остановка, ошибки (`daemon`) | `sudo grep disk_monitor /var/log/syslog` или `sudo journalctl -t disk_monitor` |
| система без rsyslog, события | `sudo journalctl -t disk_monitor SYSLOG_FACILITY=16 -f` |
| система без rsyslog, служебные | `sudo journalctl -t disk_monitor SYSLOG_FACILITY=3` |

Формат строки в `/var/log/disk_monitor.log`:

```
<время> <хост> disk_monitor[<pid>]: <события> <file|dir> <путь> [(cookie N)]
```

| Событие | Что произошло |
|---|---|
| `OPEN` | файл или каталог открыт |
| `ACCESS` | чтение (для каталога - чтение списка файлов) |
| `MODIFY` | запись в файл |
| `CLOSE_WRITE` / `CLOSE_NOWRITE` | закрыт после записи / без записи |
| `ATTRIB` | изменены права, владелец, время и т.п. |
| `CREATE` / `DELETE` | создан / удалён |
| `MOVED_FROM` / `MOVED_TO` | переименование или перемещение; у пары одинаковый cookie |
| `DELETE_SELF` / `MOVE_SELF` | удалён или перемещён сам наблюдаемый каталог из конфига |

