# Disk monitor - вариант №20

## Задание:

Disk monitor. Отслеживать использование файлов в указанных в конфигурационном файле папках (inotify), записывать каждый акт взаимодействия в отдельный log-файл (настроить свой LOG_LOCAL0).

Проект не использует сторонние C++ библиотеки. Сборка не устанавливает пакеты, не изменяет `/etc` и не запускает службы.

## 1. Структура проекта

```text
lab1/
├── build.sh
├── README.md
├── TEST_RESULTS.md
├── include/disk_monitor/
│   ├── Config.hpp
│   ├── Daemon.hpp
│   ├── InotifyEventPolicy.hpp
│   ├── Logging.hpp
│   ├── ProcessControl.hpp
│   └── WatchManager.hpp
├── src/
│   ├── Config.cpp
│   ├── Daemon.cpp
│   ├── Logging.cpp
│   ├── ProcessControl.cpp
│   ├── WatchManager.cpp
│   ├── WatchManagerEvents.cpp
│   └── main.cpp
├── config/
│   ├── disk-monitor.conf.example
│   ├── logrotate/disk-monitor
│   └── rsyslog/30-disk-monitor.conf
├── scripts/install-rsyslog-config.sh
└── tests/
    ├── run_all.sh
    ├── run_tests.sh
    ├── test_lifecycle.sh
    ├── test_events.sh
    ├── test_reload.sh
    ├── test_log_sink.sh
    ├── test_overflow_live.sh
    ├── test_overflow_unit.cpp
    ├── test_overflow_unit.sh
    ├── test_rsyslog.sh
    └── support/
        ├── common.sh
        └── syslog_capture.c
```

## Сборка и запуск

Требуются Linux и `g++` с поддержкой C++17.

```bash
./build.sh
./disk-monitor --help
cp config/disk-monitor.conf.example ./disk-monitor.conf
```

Перед запуском отредактируйте `watch_dir` в `disk-monitor.conf`, указав существующие каталоги для проверки. Запуск в foreground:

```bash
./disk-monitor --foreground \
  --config ./disk-monitor.conf \
  --pid-file /tmp/disk-monitor.pid
```

Без `--foreground` программа запускается как демон. По умолчанию конфигурация называется `disk-monitor.conf` и ищется в текущем каталоге при запуске. `SIGHUP` перечитывает конфигурацию, `SIGINT` и `SIGTERM` завершают программу.

## Конфигурация и журнал

Поддерживаются директивы `watch_dir` (можно указать несколько каталогов) и `exclude_path` (путь, который нужно исключить из наблюдения, например файл журнала). Относительные пути считаются от каталога конфигурационного файла.

Пример:

```ini
watch_dir=/home/user/Documents
watch_dir=/home/user/Downloads
exclude_path=/var/log/disk_monitor.log
```

Пример правила для записи `LOG_LOCAL0` в `/var/log/disk_monitor.log` находится в `config/rsyslog/30-disk-monitor.conf`. Установка системного правила — необязательный отдельный шаг, требующий прав администратора:

```bash
sudo ./scripts/install-rsyslog-config.sh
```

## Проверки

Запустить автоматические тесты:

```bash
./tests/run_all.sh
```
Подробная информация про результаты  тестов  описана в файле  `TEST_RESULTS.md`

## Ограничения

`inotify` не определяет пользователя или процесс, вызвавший событие. При переполнении очереди программа восстанавливает наблюдение, но не может восстановить точную последовательность пропущенных событий.