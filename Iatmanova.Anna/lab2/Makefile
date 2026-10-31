.PHONY: all clean test

all:
	./build.sh

clean:
	rm -rf .build host_mq host_fifo host_sock
	rm -f /tmp/os_lab2_v13_*.fifo /tmp/os_lab2_v13_*.sock

test: all
	./run_tests.sh --skip-build
