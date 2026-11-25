.PHONY: all test test-idle clean
all:
	./build.sh $(CMAKE_ARGS)
test:
	ctest --test-dir build --output-on-failure --no-tests=error
test-idle:
	xvfb-run -a python3 tests/integration.py --bin-dir build/bin --transport sock --idle
	xvfb-run -a python3 tests/integration.py --bin-dir build/bin --transport fifo --idle
clean:
	cmake -E rm -rf build
