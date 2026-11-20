.PHONY: all clean

BUILD_DIR = build

all:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j

clean:
	rm -rf $(BUILD_DIR)
	rm -f host_mq host_fifo host_sock