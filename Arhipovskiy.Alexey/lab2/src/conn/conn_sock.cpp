#include "conn/conn.h"

#include "ipc/FdIo.h"
#include "ipc/Names.h"
#include "ipc/Timeouts.h"
#include "ipc/UniqueFd.h"
#include "util/SystemError.h"

#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace chat {

namespace {

sockaddr_un makeAddress(const std::string& path)
{
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        throwSystemError("socket path too long: " + path, ENAMETOOLONG);
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    return address;
}

UniqueFd makeSocket()
{
    UniqueFd fd(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    if (!fd.valid()) {
        throwSystemError("socket");
    }
    return fd;
}

}

// Unix domain stream socket: the host listens on a per-client path and accepts one connection
struct Conn::Impl {
    std::string path;
    bool host = false;
    UniqueFd listener;
    UniqueFd stream;

    bool ensureConnected()
    {
        if (stream.valid()) {
            return true;
        }
        if (!listener.valid() || !fdio::waitReadable(listener.get(), kIpcTimeout)) {
            return false;
        }
        stream.reset(::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC));
        listener.reset();
        ::unlink(path.c_str());
        return stream.valid();
    }
};

Conn::Conn(const ConnId& id, bool create) : impl_(std::make_unique<Impl>())
{
    impl_->path = names::socketPath(id);
    impl_->host = create;
    const sockaddr_un address = makeAddress(impl_->path);
    const auto* generic = reinterpret_cast<const sockaddr*>(&address);

    if (create) {
        impl_->listener = makeSocket();
        ::unlink(impl_->path.c_str());
        if (::bind(impl_->listener.get(), generic, sizeof(address)) != 0) {
            throwSystemError("bind " + impl_->path);
        }
        if (::listen(impl_->listener.get(), 1) != 0) {
            throwSystemError("listen " + impl_->path);
        }
        return;
    }

    impl_->stream = makeSocket();
    if (::connect(impl_->stream.get(), generic, sizeof(address)) != 0) {
        throwSystemError("connect " + impl_->path);
    }
}

Conn::~Conn()
{
    if (impl_->host && impl_->listener.valid()) {
        ::unlink(impl_->path.c_str());
    }
}

bool Conn::Read(void* buf, std::size_t count)
{
    return impl_->ensureConnected() && fdio::readExact(impl_->stream.get(), buf, count);
}

bool Conn::Write(const void* buf, std::size_t count)
{
    return impl_->ensureConnected() && fdio::sendExact(impl_->stream.get(), buf, count);
}

const char* Conn::typeCode()
{
    return "sock";
}

}
