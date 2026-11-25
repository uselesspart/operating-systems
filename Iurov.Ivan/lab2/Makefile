.PHONY: all test clean

all:
	./build.sh

test:
	cmake -S . -B build-tests -DWOLF_BUILD_GUI=ON -DWOLF_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
	cmake --build build-tests --parallel "$$(nproc)"
	ctest --test-dir build-tests --output-on-failure
	rm -rf build-tests

clean:
	rm -rf build-tests
	rm -f host_*
