#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>
#include <system_error>
#include <thread>

#include "conn/conn.h"
#include "ipc/FdIo.h"
#include "ipc/Names.h"
#include "ipc/Timeouts.h"
#include "ipc/UniqueFd.h"

namespace wolf {

namespace {

[[noreturn]] void fail(const std::string& what, int err = errno) {
    throw std::system_error(err, std::generic_category(), what);
}

void makeFifo(const std::string& path) {
    ::unlink(path.c_str());
    if (::mkfifo(path.c_str(), 0600) != 0) fail("mkfifo " + path);
}

UniqueFd openFifo(const std::string& path, int mode) {
    UniqueFd fd(::open(path.c_str(), mode | O_NONBLOCK | O_CLOEXEC));
    if (!fd.valid()) fail("open " + path);
    return fd;
}

UniqueFd openFifoWriter(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + kIpcTimeout;
    for (;;) {
        UniqueFd fd(::open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
        if (fd.valid()) return fd;
        if (errno != ENXIO) fail("open " + path);
        if (std::chrono::steady_clock::now() >= deadline) fail("open " + path, ETIMEDOUT);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

}

struct Conn::Impl {
    bool host = false;
    std::string upPath;
    std::string downPath;
    UniqueFd in;
    UniqueFd out;
    bool peerSeen = false;

    bool ensureOutput() {
        if (out.valid()) return true;
        const auto deadline = std::chrono::steady_clock::now() + kIpcTimeout;
        for (;;) {
            UniqueFd fd(::open(downPath.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
            if (fd.valid()) {
                out = std::move(fd);
                return true;
            }
            if (errno != ENXIO) return false;
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
};

void Conn::prepare(const ConnId& id) {
    const std::string dir = names::runtimeDir(id.hostPid);
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) fail("mkdir " + dir);
    makeFifo(names::fifoPath(id, names::kToHost));
    makeFifo(names::fifoPath(id, names::kToClient));
}

void Conn::discard(const ConnId& id) {
    ::unlink(names::fifoPath(id, names::kToHost).c_str());
    ::unlink(names::fifoPath(id, names::kToClient).c_str());
}

Conn::Conn(const ConnId& id, bool create) : impl_(std::make_unique<Impl>()) {
    impl_->host = create;
    impl_->upPath = names::fifoPath(id, names::kToHost);
    impl_->downPath = names::fifoPath(id, names::kToClient);

    if (create) {
        impl_->in = openFifo(impl_->upPath, O_RDONLY);
        return;
    }

    impl_->out = openFifoWriter(impl_->upPath);
    impl_->in = openFifo(impl_->downPath, O_RDONLY);
    fdio::closeAllExcept({impl_->in.get(), impl_->out.get()});
}

Conn::~Conn() {
    if (impl_->host) {
        ::unlink(impl_->upPath.c_str());
        ::unlink(impl_->downPath.c_str());
    }
}

bool Conn::Read(void* buf, std::size_t count) {
    if (!impl_->peerSeen) {
        if (!fdio::waitReadable(impl_->in.get(), kIpcTimeout)) return false;
        impl_->peerSeen = true;
    }
    return fdio::readExact(impl_->in.get(), buf, count, kIpcTimeout);
}

bool Conn::Write(const void* buf, std::size_t count) {
    if (impl_->host && !impl_->ensureOutput()) return false;
    return fdio::writeExact(impl_->out.get(), buf, count, kIpcTimeout);
}

const char* Conn::typeCode() { return "fifo"; }

}
