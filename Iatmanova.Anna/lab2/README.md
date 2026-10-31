# ОС, лабораторная №2 — вариант 13

## Что реализовано

Вариант 13:

- сценарий 2 — локальный чат;
- конфигурация: родственные процессы, один к одному;
- клиент создаётся хостом через `fork()`;
- handshake не нужен;
- синхронизация: локальный POSIX-семафор `sem_init(..., pshared=1, ...)` в общей `mmap`-памяти;
- три IPC-варианта с одинаковым интерфейсом `Conn`:
  - `conn_mq.cpp` — POSIX message queues (`mq_open`);
  - `conn_fifo.cpp` — именованные каналы (`mkfifo`);
  - `conn_sock.cpp` — UNIX domain socket;
- GUI хоста реализован на Qt 6 Widgets;
- клиент работает в терминале;
- хост и клиент могут отправлять общие и личные сообщения;
- сообщения идут по одному IPC-каналу в порядке отправки;
- если клиент не отправляет сообщения 60 секунд, хост посылает ему `SIGKILL`;
- ошибки выводятся в stderr, статусы соединения/завершения показываются в GUI;
- ожидания локального семафора ограничены тайм-аутом 5 секунд;
- присутствуют автоматические тесты.

> В конфигурации «родственные, один к одному» отдельный `client.cpp` не требуется: клиент является дочерним процессом `host.cpp`.

## Что установить на Ubuntu 24.04

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools
```

Проверка:

```bash
g++ --version
cmake --version
qmake6 --version
```


## Сборка

Разрешить запуск скриптов:

```bash
chmod +x build.sh run_tests.sh tests/run_tests.sh
```

Сборка по `build.sh`:

```bash
./build.sh
```

После этого в корне появятся:

```text
host_mq
host_fifo
host_sock
```

Также по требованиям можно собрать командой:

```bash
make
```

`CMakeLists.txt` автоматически находит все `conn_*.cpp`, поэтому добавление нового типа `conn_x.cpp` + `conn_x.hpp` не требует вручную добавлять новый target.

## Автоматические тесты

```bash
./run_tests.sh
```

Ожидаемый финал:

```text
[PASS] mq: parent/child round trip over IPC
[PASS] fifo: parent/child round trip over IPC
[PASS] sock: parent/child round trip over IPC
...
All tests passed.
```

Тесты проверяют:

1. наличие требуемой структуры исходников;
2. сборку всех трёх `host_*` с `-Wall -Werror`;
3. реальный `fork()` и двусторонний обмен `ping -> pong` для `mq_open`;
4. то же для `mkfifo`;
5. то же для UNIX socket;
6. очистку промежуточной CMake-папки;
7. сборку всех вариантов командой `make` из корня проекта.

Можно отдельно проверить один IPC:

```bash
./host_mq --self-test
./host_fifo --self-test
./host_sock --self-test
```

## Как запустить чат

Например, очередь сообщений:

```bash
./host_mq
```

Или:

```bash
./host_fifo
```

```bash
./host_sock
```

После запуска:

- откроется Qt-окно **хоста**;
- клиент уже будет запущен дочерним процессом через `fork()`;
- клиент использует тот же терминал, из которого был запущен `host_*`.

### Отправка от хоста

В GUI:

1. выбрать `Общее сообщение` или `Личное клиенту`;
2. ввести текст;
3. нажать `Отправить` или Enter.

Клиент увидит сообщение в терминале.

### Отправка от клиента

В терминале:

```text
Привет
```

— общее сообщение.

Или:

```text
/all Привет всем
```

Или личное хосту:

```text
/private Привет host
```

Выход клиента:

```text
/quit
```

Если клиент ничего не отправляет 60 секунд, хост завершает его через `SIGKILL`, как указано в сценарии.

## Структура

```text
.
├── CMakeLists.txt
├── Makefile
├── build.sh
├── run_tests.sh
├── common.cpp
├── common.hpp
├── host.cpp
├── conn_mq.cpp
├── conn_mq.hpp
├── conn_fifo.cpp
├── conn_fifo.hpp
├── conn_sock.cpp
├── conn_sock.hpp
└── tests/
    └── run_tests.sh
```
