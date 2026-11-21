#include "conn/conn.hpp"

#include "link/Descriptor.hpp"
#include "link/FdOps.hpp"
#include "link/Names.hpp"
#include "link/Timeouts.hpp"
#include "util/Errors.hpp"

#include <cerrno>
#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace chatlab {

namespace {

sockaddr_un addressOf(const std::string& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        raiseErrno("socket path is too long: " + path, ENAMETOOLONG);
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return address;
}

Descriptor newSocket() {
    Descriptor fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.isOpen()) {
        raiseErrno("socket");
    }
    return fd;
}

}

struct Conn::State {
    std::string path;
    Descriptor listener;
    Descriptor stream;

    bool ensureStream() {
        if (stream.isOpen()) {
            return true;
        }
        if (!listener.isOpen() || !fdops::waitReadable(listener.get(), kWaitLimit)) {
            return false;
        }
        stream.reset(::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
        listener.reset();
        ::unlink(path.c_str());
        return stream.isOpen();
    }
};

Conn::Conn(const ConnKey& key, bool create) : state_(std::make_unique<State>()) {
    state_->path = names::socketPath(key);
    const sockaddr_un address = addressOf(state_->path);
    const auto* generic = reinterpret_cast<const sockaddr*>(&address);

    if (create) {
        state_->listener = newSocket();
        ::unlink(state_->path.c_str());
        if (::bind(state_->listener.get(), generic, sizeof(address)) != 0) {
            raiseErrno("bind " + state_->path);
        }
        if (::listen(state_->listener.get(), 1) != 0) {
            raiseErrno("listen " + state_->path);
        }
        return;
    }

    state_->stream = newSocket();
    if (::connect(state_->stream.get(), generic, sizeof(address)) != 0) {
        raiseErrno("connect " + state_->path);
    }
}

Conn::~Conn() {
    if (state_->listener.isOpen()) {
        ::unlink(state_->path.c_str());
    }
}

bool Conn::Read(void* buf, std::size_t count) {
    return state_->ensureStream() && fdops::readFull(state_->stream.get(), buf, count);
}

bool Conn::Write(const void* buf, std::size_t count) {
    return state_->ensureStream() && fdops::sendFull(state_->stream.get(), buf, count);
}

const char* Conn::Kind() {
    return "sock";
}

}
