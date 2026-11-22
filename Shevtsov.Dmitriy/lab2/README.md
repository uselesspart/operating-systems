# Лабораторная работа 2. Межпроцессное взаимодействие

## Вариант 1

Игра "Волк и семеро козлят". Конфигурация: родственные процессы, один к одному.
Типы взаимодействия:

| TYPE | Файл |
|-----------|------|
| `mmap` | `src/conn/conn_mmap.cpp` |
| `shm`  | `src/conn/conn_shm.cpp`  |
| `pipe` | `src/conn/conn_pipe.cpp` |

## Структура

```
src/
├── host.cpp      main хоста: fork козлёнка и запуск окна
├── conn/         класс Conn и его реализации conn_*.cpp, каналы на семафорах
├── game/         правила игры и формат сообщений
├── goat/         козлёнок (процесс-потомок)
├── wolf/         логика волка в отдельном потоке
└── window/       окно на Qt
tests/
└── tests.cpp
```

## Сборка и запуск

Нужны `g++`, `cmake`, `make` и Qt 5 или Qt 6

```bash
make            # или ./build.sh
./host_mmap     # ./host_shm, ./host_pipe
```

## Тесты

```bash
make test       # или ./run_tests.sh
```