#include <fcntl.h>
#include <sys/socket.h>

#include <cerrno>
#include <map>
#include <string>
#include <system_error>
#include <utility>

#include "conn/conn.h"
#include "ipc/FdIo.h"
#include "ipc/Timeouts.h"
#include "ipc/UniqueFd.h"

namespace wolf {

namespace {

std::map<int, std::pair<int, int>> g_socks;

[[noreturn]] void fail(const std::string& what, int err = errno) {
    throw std::system_error(err, std::generic_category(), what);
}

void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

}

struct Conn::Impl {
    UniqueFd fd;
};

void Conn::prepare(const ConnId& id) {
    discard(id);
    int fds[2] = {-1, -1};
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, fds) != 0) {
        fail("socketpair");
    }
    g_socks[id.clientId] = {fds[0], fds[1]};
}

void Conn::discard(const ConnId& id) {
    const auto it = g_socks.find(id.clientId);
    if (it == g_socks.end()) return;
    closeFd(it->second.first);
    closeFd(it->second.second);
    g_socks.erase(it);
}

Conn::Conn(const ConnId& id, bool create) : impl_(std::make_unique<Impl>()) {
    const auto it = g_socks.find(id.clientId);
    if (it == g_socks.end()) fail("socketpair не подготовлен для козлёнка #" + std::to_string(id.clientId), ENOENT);
    auto& fds = it->second;

    if (create) {
        if (fds.first < 0) fail("socketpair уже занят", EBUSY);
        impl_->fd.reset(fds.first);
        closeFd(fds.second);
        fds.first = -1;
        return;
    }

    if (fds.second < 0) fail("socketpair уже занят", EBUSY);
    const int fd = fds.second;
    closeFd(fds.first);
    fds.second = -1;
    impl_->fd.reset(fd);
    fdio::closeAllExcept({fd});
}

Conn::~Conn() = default;

bool Conn::Read(void* buf, std::size_t count) {
    return fdio::readExact(impl_->fd.get(), buf, count, kIpcTimeout);
}

bool Conn::Write(const void* buf, std::size_t count) {
    return fdio::writeExact(impl_->fd.get(), buf, count, kIpcTimeout);
}

const char* Conn::typeCode() { return "sock"; }

}
