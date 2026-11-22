#pragma once

#include <cstddef>
#include <memory>

constexpr int CONN_TIMEOUT_SEC = 5;

class Conn {
public:
	static void Prepare();

	Conn(int id, bool create);
	~Conn();

	Conn(const Conn&) = delete;
	Conn& operator=(const Conn&) = delete;

	bool Read(void* buf, size_t count);
	bool Write(const void* buf, size_t count);

private:
	struct Impl;
	std::unique_ptr<Impl> impl;
};
