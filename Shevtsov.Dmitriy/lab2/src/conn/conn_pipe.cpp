#include "conn.h"

#include <cerrno>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <poll.h>
#include <syslog.h>
#include <unistd.h>

namespace {

	int toClient[2] = {-1, -1};
	int toHost[2] = {-1, -1};

	void closeFd(int& fd){
		if (fd >= 0) {
			close(fd);
		}
		fd = -1;
	}

	bool waitFd(int fd, short events){
		pollfd request{fd, events, 0};
		int ready;
		while ((ready = poll(&request, 1, CONN_TIMEOUT_SEC * 1000)) < 0 && errno == EINTR) {}

		if (ready < 0) {
			syslog(LOG_ERR, "poll: %m");
		}
		if (ready == 0) {
			syslog(LOG_ERR, "pipe wait timed out after %d s", CONN_TIMEOUT_SEC);
		}
		return ready > 0;
	}

}

struct Conn::Impl {
	int in = -1;
	int out = -1;
};

void Conn::Prepare(){
	if (pipe(toClient) < 0 || pipe(toHost) < 0) {
		throw std::system_error(errno, std::generic_category(), "pipe");
	}
}

Conn::Conn(int, bool create) : impl(std::make_unique<Impl>()){
	int* in = toClient;
	int* out = toHost;
	if (create) {
		in = toHost;
		out = toClient;
	}

	if (in[0] < 0 || out[1] < 0) {
		throw std::logic_error("Conn::Prepare() must be called before fork()");
	}

	impl->in = std::exchange(in[0], -1);
	impl->out = std::exchange(out[1], -1);
	closeFd(in[1]);
	closeFd(out[0]);
}

Conn::~Conn(){
	closeFd(impl->in);
	closeFd(impl->out);
}

bool Conn::Read(void* buf, size_t count){
	auto* bytes = static_cast<char*>(buf);
	bool ok = true;
	while (ok && count > 0) {
		ok = waitFd(impl->in, POLLIN);
		if (ok) {
			ssize_t received = read(impl->in, bytes, count);
			if (received > 0) {
				bytes += received;
				count -= received;
			} else if (received == 0) {
				syslog(LOG_ERR, "pipe is closed by the other side");
				ok = false;
			} else if (errno != EINTR) {
				syslog(LOG_ERR, "read: %m");
				ok = false;
			}
		}
	}
	return ok;
}

bool Conn::Write(const void* buf, size_t count){
	auto* bytes = static_cast<const char*>(buf);
	bool ok = true;
	while (ok && count > 0) {
		ok = waitFd(impl->out, POLLOUT);
		if (ok) {
			ssize_t sent = write(impl->out, bytes, count);
			if (sent >= 0) {
				bytes += sent;
				count -= sent;
			} else if (errno != EINTR) {
				syslog(LOG_ERR, "write: %m");
				ok = false;
			}
		}
	}
	return ok;
}
