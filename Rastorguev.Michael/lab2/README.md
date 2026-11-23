# Межпроцессорное взаимодействие

## Вариант 1: 
Сценарий 1, Родственные, Один к одному, mmap, shm, pipe

## Расположение реализаций взаимодействий
`src/implementions/conns`

## Сборка и запуск
```bash
make            # или ./build.sh
./host_mmap     # ./host_shm, ./host_pipe
```

## Тестирование

```bash
make test       # или ./run_tests.sh
```