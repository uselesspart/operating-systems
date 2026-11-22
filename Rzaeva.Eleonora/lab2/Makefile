.PHONY: all test clean

hosts := $(patsubst src/conn_%.cpp,host_%,$(wildcard src/conn_*.cpp))

all:
	bash ./build.sh

test:
	bash ./build.sh test

clean:
	rm -f -- $(hosts)
