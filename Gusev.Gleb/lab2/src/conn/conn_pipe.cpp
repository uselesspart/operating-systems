#include <fcntl.h>
#include <unistd.h>

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

struct PipePair {
    int toKid[2] = {-1, -1};
    int toHost[2] = {-1, -1};
};

std::map<int, PipePair> g_pipes;

[[noreturn]] void fail(const std::string& what, int err = errno) {
    throw std::system_error(err, std::generic_category(), what);
}

void closeFd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

}

struct Conn::Impl {
    UniqueFd in;
    UniqueFd out;
};

void Conn::prepare(const ConnId& id) {
    discard(id);
    PipePair pair;
    if (::pipe2(pair.toKid, O_CLOEXEC | O_NONBLOCK) != 0) fail("pipe2 toKid");
    if (::pipe2(pair.toHost, O_CLOEXEC | O_NONBLOCK) != 0) {
        const int err = errno;
        closeFd(pair.toKid[0]);
        closeFd(pair.toKid[1]);
        fail("pipe2 toHost", err);
    }
    g_pipes[id.clientId] = pair;
}

void Conn::discard(const ConnId& id) {
    const auto it = g_pipes.find(id.clientId);
    if (it == g_pipes.end()) return;
    for (int& fd : it->second.toKid) closeFd(fd);
    for (int& fd : it->second.toHost) closeFd(fd);
    g_pipes.erase(it);
}

Conn::Conn(const ConnId& id, bool create) : impl_(std::make_unique<Impl>()) {
    const auto it = g_pipes.find(id.clientId);
    if (it == g_pipes.end()) fail("pipe не подготовлен для козлёнка #" + std::to_string(id.clientId), ENOENT);
    PipePair& pair = it->second;

    if (create) {
        if (pair.toHost[0] < 0 || pair.toKid[1] < 0) fail("pipe уже занят", EBUSY);
        impl_->in.reset(pair.toHost[0]);
        impl_->out.reset(pair.toKid[1]);
        closeFd(pair.toKid[0]);
        closeFd(pair.toHost[1]);
        pair.toHost[0] = -1;
        pair.toKid[1] = -1;
        return;
    }

    if (pair.toKid[0] < 0 || pair.toHost[1] < 0) fail("pipe уже занят", EBUSY);
    const int in = pair.toKid[0];
    const int out = pair.toHost[1];
    closeFd(pair.toKid[1]);
    closeFd(pair.toHost[0]);
    pair.toKid[0] = -1;
    pair.toHost[1] = -1;
    impl_->in.reset(in);
    impl_->out.reset(out);
    fdio::closeAllExcept({in, out});
}

Conn::~Conn() = default;

bool Conn::Read(void* buf, std::size_t count) {
    return fdio::readExact(impl_->in.get(), buf, count, kIpcTimeout);
}

bool Conn::Write(const void* buf, std::size_t count) {
    return fdio::writeExact(impl_->out.get(), buf, count, kIpcTimeout);
}

const char* Conn::typeCode() { return "pipe"; }

}
