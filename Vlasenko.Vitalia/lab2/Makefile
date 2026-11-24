.DEFAULT_GOAL := all

.PHONY: all test clean format

all:
	./build.sh

test:
	./build.sh --test

clean:
	rm -rf bin build

format:
	find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) \
		-exec clang-format -i {} +
