## Задание 

Напоминалка. В конфигурационный файл пользователь может добавлять с помощью специального скрипта новые события для напоминания в формате “add_event date time [flag] text”, где flag - необязательное поле, значения “-h -d -w” означают повтор напоминаний каждый час, день, неделю, соответственно. В указанное время демон должен открывать новый терминал и выводить заданный текст. Интервал срабатывания: 10 секунд.

## Сборка

```bash
./build.sh
```

После сборки появится файл `reminder_daemon`.

## Запуск

По умолчанию используется `config.conf`:

```bash
./reminder_daemon
```

Можно указать другой файл конфигурации:

```bash
./reminder_daemon /path/to/config.conf
```

Демон работает в фоне. Его PID хранится в `reminder.pid` рядом с конфигурацией.

## Конфигурация

Пример `config.conf`:

```ini
interval=10
state_dir=./state
terminal=auto
```

- `interval` — интервал проверки в секундах.
- `state_dir` — каталог для состояния напоминаний.
- `terminal` — `auto`, `xterm`, `gnome-terminal`, `konsole` или `x-terminal-emulator`.

События имеют формат:

```text
event YYYY-MM-DD HH:MM [-h|-d|-w] текст
```

Флаг повтора необязателен:

- без флага — один раз;
- `-h` — каждый час;
- `-d` — каждый день;
- `-w` — каждую неделю.

Время указывается в местном часовом поясе.

## Добавление напоминания

```bash
./add_event 2026-10-27 15:30 Submit homework
./add_event 2026-10-27 16:00 -h Drink water
./add_event 2026-10-27 18:00 -d Practice English
./add_event 2026-10-27 19:00 -w Weekly review
```

Скрипт добавляет событие в конфигурацию и отправляет демону `SIGHUP`, чтобы он перечитал файл.

Для другой конфигурации можно задать переменную:

```bash
REMINDER_CONFIG=/path/to/config.conf ./add_event 2026-10-27 15:30 Test
```

## Сигналы

```bash
kill -HUP "$(cat reminder.pid)"   # перечитать конфигурацию
kill -TERM "$(cat reminder.pid)"  # остановить демон
```

Логи доступны через systemd journal:

```bash
journalctl -t reminder_daemon
```

## Тесты

```bash
./tests/run_tests.sh
```
