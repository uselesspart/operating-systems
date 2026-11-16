.PHONY: all clean test

all:
	./build.sh

clean:
	rm -rf .build .autocompile_test_build .conn_probe_test_build
	rm -f host_*
	rm -f /tmp/os_lab2_v13_*.fifo /tmp/os_lab2_v13_*.sock

test: all
	./run_tests.sh --skip-build
