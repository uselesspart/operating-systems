# Обёртка: "make" в папке проекта собирает все host_* и client_* для всех conn_*.cpp (через build.sh -> cmake).
.PHONY: all nogui test clean

all:
	./build.sh

nogui:
	./build.sh --no-gui

test:
	bash tests/run_tests.sh

clean:
	rm -rf bin
