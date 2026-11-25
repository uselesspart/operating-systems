#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "common.h"
#include "conn.h"

namespace {

sockaddr_un socketAddress(int id) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::string path = "/tmp/wolf_" + std::to_string(id) + ".sock";
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    return addr;
}

std::runtime_error sysError(const char* what) {
    return std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

}

const char* Conn::Type() {
    return "sock";
}

Conn::Conn(int id, bool create) : id_(id), create_(create) {
    sockaddr_un addr = socketAddress(id);
    auto* sa = reinterpret_cast<sockaddr*>(&addr);
    if (create) {
        int listener = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (listener < 0) throw sysError("socket");
        unlink(addr.sun_path);
        if (bind(listener, sa, sizeof(addr)) < 0 || listen(listener, 1) < 0) {
            auto error = sysError("bind");
            close(listener);
            throw error;
        }
        pollfd pfd{listener, POLLIN, 0};
        if (poll(&pfd, 1, kTimeoutSec * 1000) == 1) {
            rd_ = wr_ = accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
        }
        close(listener);
        unlink(addr.sun_path);
        if (rd_ < 0) throw std::runtime_error("socket: goat did not connect in time");
    } else {
        int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0) throw sysError("socket");
        bool connected = false;
        retryWithTimeout([&] {
            connected = connect(fd, sa, sizeof(addr)) == 0;
            return connected || (errno != ENOENT && errno != ECONNREFUSED);
        });
        if (!connected) {
            auto error = sysError("connect");
            close(fd);
            throw error;
        }
        rd_ = wr_ = fd;
    }
}

Conn::~Conn() {
    closeAll();
}

void Conn::closeAll() {
    if (rd_ >= 0) close(rd_);
    rd_ = wr_ = -1;
}

bool Conn::Read(void* buf, size_t count) {
    return readAll(rd_, buf, count);
}

bool Conn::Write(const void* buf, size_t count) {
    return writeAll(wr_, buf, count);
}
