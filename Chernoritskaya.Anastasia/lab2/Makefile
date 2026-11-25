.PHONY: all clean test

all:
	./build.sh

clean:
	rm -rf build
	rm -f host_* client_* unit_tests
	rm -f /tmp/my_*.fifo /tmp/my_*.sock /tmp/my_*

test: all
	./run_tests.sh