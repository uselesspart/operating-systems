# Прокладка, обеспечивающая требование: "вызов make в папке проекта
# собирает все исполняемые файлы host_* (и тесты при make test)".
# Фактическая сборка выполняется CMake через build.sh.
.PHONY: all build test clean

all: build

build:
	bash build.sh build

test:
	bash build.sh test

clean:
	rm -f host_seg host_mq host_fifo test_game_* test_connection_*
	rm -rf .build.* build
