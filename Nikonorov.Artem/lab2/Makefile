.PHONY: all test clean

all:
	./build.sh

test:
	cmake -S . -B build-tests -DWOLF_BUILD_GUI=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
	cmake --build build-tests --parallel "$$(nproc)"
	ctest --test-dir build-tests --output-on-failure

clean:
	rm -rf build build-tests
	find bin -maxdepth 1 -type f -name 'host_*' -delete 2>/dev/null || true
