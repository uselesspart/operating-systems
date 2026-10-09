#include "conn/conn.h"

#include "ipc/FdIo.h"
#include "ipc/Names.h"
#include "ipc/Timeouts.h"
#include "ipc/UniqueFd.h"
#include "util/SystemError.h"

#include <cerrno>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chat {

namespace {

constexpr auto kRetryInterval = std::chrono::milliseconds(20);

UniqueFd openFifo(const std::string& path, int mode)
{
    UniqueFd fd(::open(path.c_str(), mode | O_NONBLOCK | O_CLOEXEC));
    if (!fd.valid()) {
        throwSystemError("open " + path);
    }
    return fd;
}

void makeFifo(const std::string& path)
{
    ::unlink(path.c_str());
    if (::mkfifo(path.c_str(), 0600) != 0) {
        throwSystemError("mkfifo " + path);
    }
}

}

// A pair of named pipes per client: "up" carries client -> host, "down" host -> client.
// Write ends can only be opened once the other side holds the read end, so both sides
// open their read end first and the host opens its write end lazily
struct Conn::Impl {
    std::string inPath;
    std::string outPath;
    bool host = false;
    bool peerConnected = false;
    UniqueFd in;
    UniqueFd out;

    bool openOutput()
    {
        const auto deadline = std::chrono::steady_clock::now() + kIpcTimeout;
        do {
            const int fd = ::open(outPath.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0) {
                out.reset(fd);
                fdio::setBlocking(in.get(), true);
                fdio::setBlocking(out.get(), true);
                return true;
            }
            if (errno != ENXIO) {
                return false;
            }
            std::this_thread::sleep_for(kRetryInterval);
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }

    // Host: the write end opens once the client holds its read end (the client opens "up" first)
    bool connectToClient()
    {
        if (!peerConnected) {
            peerConnected = openOutput();
        }
        return peerConnected;
    }

    // Client: "down" reads as end of file until the host opens it for writing,
    // so the first read waits for the host's data
    bool awaitHost()
    {
        if (!peerConnected) {
            peerConnected = fdio::waitReadable(in.get(), kIpcTimeout);
        }
        return peerConnected;
    }
};

Conn::Conn(const ConnId& id, bool create) : impl_(std::make_unique<Impl>())
{
    const std::string toHost = names::fifoPath(id, names::kToHost);
    const std::string toClient = names::fifoPath(id, names::kToClient);
    impl_->host = create;
    impl_->inPath = create ? toHost : toClient;
    impl_->outPath = create ? toClient : toHost;

    if (create) {
        makeFifo(toHost);
        makeFifo(toClient);
        impl_->in = openFifo(impl_->inPath, O_RDONLY);
        return;
    }

    impl_->out = openFifo(impl_->outPath, O_WRONLY);
    impl_->in = openFifo(impl_->inPath, O_RDONLY);
    fdio::setBlocking(impl_->in.get(), true);
    fdio::setBlocking(impl_->out.get(), true);
}

Conn::~Conn()
{
    if (impl_->host) {
        ::unlink(impl_->inPath.c_str());
        ::unlink(impl_->outPath.c_str());
    }
}

bool Conn::Read(void* buf, std::size_t count)
{
    const bool connected = impl_->host ? impl_->connectToClient() : impl_->awaitHost();
    return connected && fdio::readExact(impl_->in.get(), buf, count);
}

bool Conn::Write(const void* buf, std::size_t count)
{
    const bool connected = !impl_->host || impl_->connectToClient();
    return connected && fdio::writeExact(impl_->out.get(), buf, count);
}

const char* Conn::typeCode()
{
    return "fifo";
}

}
