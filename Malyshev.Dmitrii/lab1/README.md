# Лабораторная 1. Демон (вариант 20, Disk monitor)

**Задание:** отслеживать использование файлов в папках из конфигурационного файла (inotify)
и записывать каждый акт взаимодействия в отдельный log-файл (свой `LOG_LOCAL0`).

## Структура

```
build.sh          сборка (g++ -Wall -Werror, без промежуточных файлов)
setup_syslog.sh   настройка отдельного log-файла для LOG_LOCAL0 (один раз)
config.conf       пример конфигурационного файла
src/main.cpp      точка входа
src/Daemon.hpp    класс-синглтон Daemon
src/Daemon.cpp    реализация
tests/test.sh     автотесты
```

## Быстрый старт

```bash
bash setup_syslog.sh          # один раз: local0 -> /var/log/lab1_disk_monitor.log
./build.sh
mkdir -p watched1 watched2    # папки из config.conf
./lab1_daemon                 # или ./lab1_daemon /path/to/my.conf
sudo tail -f /var/log/lab1_disk_monitor.log   # во втором окне
```

Любые действия с файлами в `watched1` и `watched2` попадают в журнал:

```
lab1_daemon[123]: CREATE file /home/me/lab1/watched1/a.txt
lab1_daemon[123]: OPEN file /home/me/lab1/watched1/a.txt
lab1_daemon[123]: MODIFY file /home/me/lab1/watched1/a.txt
lab1_daemon[123]: CLOSE_WRITE file /home/me/lab1/watched1/a.txt
```

События: `ACCESS MODIFY ATTRIB OPEN CLOSE_WRITE CLOSE_NOWRITE CREATE DELETE MOVED_FROM
MOVED_TO DELETE_SELF MOVE_SELF`; пометка `file`/`dir` показывает, файл это или каталог.

## Конфигурационный файл

```
# комментарии начинаются с '#'
folder = ./watched1     # строк folder может быть несколько
folder = ./watched2     # относительный путь считается от каталога конфига
recursive = yes         # следить и за подпапками (по умолчанию yes)
```

Путь к конфигу превращается в абсолютный до `chdir("/")` и запоминается, поэтому
SIGHUP работает после демонизации.

## Два журнала

| Что | Фасилити | Куда попадает |
|---|---|---|
| события диска (OPEN, CREATE, ...) | `LOG_LOCAL0` | `/var/log/lab1_disk_monitor.log` (правило rsyslog из `setup_syslog.sh`) |
| служебные: запуск, ошибки, SIGHUP, выход | `LOG_DAEMON` | обычный syslog / `journalctl -t lab1_daemon` |

Папку `/var/log` (и её родителей при `recursive = yes`) отслеживать нельзя: журнал пишет в неё
сам, возник бы бесконечный цикл. Демон откажется стартовать с таким конфигом.

## Управление

| Действие | Команда |
|---|---|
| перечитать конфиг | `kill -HUP $(cat /tmp/lab1_daemon.pid)` |
| остановить | `kill -TERM $(cat /tmp/lab1_daemon.pid)` |
| служебный журнал | `journalctl -t lab1_daemon -f` |

Повторный запуск: новый процесс читает `/tmp/lab1_daemon.pid`, проверяет через `/proc/<pid>`,
что процесс жив (и что это именно этот демон - по `/proc/<pid>/comm`), отправляет ему SIGTERM,
ждёт завершения, после демонизации записывает свой pid.

## Как устроено

1. Читается и проверяется конфиг, ошибки выводятся в терминал и в syslog.
2. Останавливается предыдущий экземпляр (если есть).
3. Демонизация: `fork` -> родитель выходит -> `setsid` -> `chdir("/")` -> закрытие всех
   дескрипторов -> 0, 1, 2 открываются на `/dev/null`.
4. Записывается pid-файл, создаются inotify-watch для всех папок конфига (и подпапок).
5. Цикл: `ppoll` по дескриптору inotify; пришли события - каждое пишется в журнал. Новые
   подпапки получают watch автоматически. Сигналы заблокированы везде, кроме самого ожидания,
   поэтому SIGHUP/SIGTERM не теряются и будят демон сразу.
6. SIGHUP - перечитать конфиг и пересоздать watch, SIGTERM - запись в журнал и выход.

## Ограничения

- inotify не сообщает, какой процесс обратился к файлу - журнал фиксирует сам факт события.
- Вложенные папки, созданные в первые микросекунды после родителя (`mkdir -p a/b/c`),
  могут остаться без watch.
- Лимит числа watch задаёт `fs.inotify.max_user_watches`; при его исчерпании демон пишет ошибку.

## Тесты

```bash
bash setup_syslog.sh   # один раз
bash tests/test.sh
```

Проверяются: сборка, ошибочные конфиги, запуск, события в двух папках и во вложенной подпапке,
игнорирование чужих папок, SIGHUP (смена папок), защита от повторного запуска, SIGTERM и
удаление pid-файла. Если `setup_syslog.sh` не выполнен, проверки по журналу пропускаются (SKIP).
