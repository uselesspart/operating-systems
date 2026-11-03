BUILD_DIR := build
TARGETS   := host_mq host_fifo host_sock

.PHONY: all clean

all:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake .. -DCMAKE_BUILD_TYPE=Release >/dev/null
	@cd $(BUILD_DIR) && make -j$$(nproc)
	@for t in $(TARGETS); do cp $(BUILD_DIR)/$$t .; done

clean:
	rm -rf $(BUILD_DIR) $(TARGETS)