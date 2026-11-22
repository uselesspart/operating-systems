# Лабораторная работа 2. Локальный чат — вариант №16

Независимые хост и один клиент. Обмен через POSIX-очереди, FIFO и Unix-сокеты.
Linux 5.3+, glibc 2.30+, C++17, Qt 6.

## Сборка

```bash
sudo apt install build-essential cmake qt6-base-dev
make
```

Также можно собрать через `./build.sh`.

| IPC | Хост | Клиент |
| --- | --- | --- |
| Очереди сообщений | `host_mq` | `client_mq` |
| FIFO | `host_fifo` | `client_fifo` |
| Сокеты | `host_sock` | `client_sock` |

## Запуск

Из папки проекта в первом терминале:

```bash
./bin/host_fifo
```

Во втором терминале введите PID, указанный в окне хоста:

```bash
read -r -p "Host PID: " pid
./bin/client_fifo "$pid"
```

Для другого IPC замените `fifo` на `mq` или `sock` в обеих командах.

`Everyone` отправляет общее сообщение, `Host` и `Client` — личное выбранному
участнику. Отправка — кнопкой `Send` или Enter. Клиент без отправленных
сообщений больше минуты завершается. Для остановки закройте окна.

## Тесты

```bash
./tests/run.sh
./tests/run.sh --sanitize
```
