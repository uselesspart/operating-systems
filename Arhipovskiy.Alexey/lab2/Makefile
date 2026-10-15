# make        - собрать все host_* и client_* (через build.sh и CMake)
# make test   - собрать и прогнать все тесты
# make clean  - удалить собранные программы

.PHONY: all test clean

all:
	@sh build.sh

test:
	@bash tests/run_tests.sh

clean:
	rm -f host_* client_*
