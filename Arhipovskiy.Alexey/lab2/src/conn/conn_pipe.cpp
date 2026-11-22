#include "conn/conn.h"

#include "ipc/FdIo.h"
#include "ipc/Timeouts.h"
#include "ipc/UniqueFd.h"
#include "util/SystemError.h"

#include <string>

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

namespace chat {

namespace {

// The host keeps the client's pipe ends at fixed descriptor numbers derived from the client id,
// so the independent client process can open them as /proc/<host pid>/fd/<number>
constexpr int kFirstClientFd = 1000;

int clientReadFd(int clientId)
{
    return kFirstClientFd + 2 * clientId;
}

int clientWriteFd(int clientId)
{
    return kFirstClientFd + 2 * clientId + 1;
}

std::string procFdPath(pid_t pid, int fd)
{
    return "/proc/" + std::to_string(pid) + "/fd/" + std::to_string(fd);
}

void ensureDescriptorLimit(int highestFd)
{
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        throwSystemError("getrlimit");
    }
    const auto needed = static_cast<rlim_t>(highestFd) + 1;
    if (limit.rlim_cur >= needed) {
        return;
    }
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < needed) {
        throwSystemError("descriptor limit is too low for client pipes", EMFILE);
    }
    limit.rlim_cur = needed;
    if (::setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        throwSystemError("setrlimit");
    }
}

UniqueFd moveTo(UniqueFd fd, int target)
{
    if (::fcntl(target, F_GETFD) != -1) {
        throwSystemError("descriptor " + std::to_string(target) + " is already in use", EBUSY);
    }
    if (::dup3(fd.get(), target, O_CLOEXEC) < 0) {
        throwSystemError("dup3");
    }
    return UniqueFd(target);
}

UniqueFd openPeerEnd(pid_t hostPid, int fd, int mode)
{
    const std::string path = procFdPath(hostPid, fd);
    UniqueFd result(::open(path.c_str(), mode | O_CLOEXEC));
    if (!result.valid()) {
        throwSystemError("open " + path);
    }
    return result;
}

}

// Two anonymous pipes created by the host. The host holds the client's ends until the
// client has opened them through /proc, then closes its copies so that EOF and EPIPE work
struct Conn::Impl {
    UniqueFd in;
    UniqueFd out;
    UniqueFd clientIn;
    UniqueFd clientOut;

    bool waitingForClient() const { return clientIn.valid(); }
};

Conn::Conn(const ConnId& id, bool create) : impl_(std::make_unique<Impl>())
{
    if (!create) {
        impl_->in = openPeerEnd(id.hostPid, clientReadFd(id.clientId), O_RDONLY);
        impl_->out = openPeerEnd(id.hostPid, clientWriteFd(id.clientId), O_WRONLY);
        return;
    }

    ensureDescriptorLimit(clientWriteFd(id.clientId));
    int toClient[2];
    int toHost[2];
    if (::pipe2(toClient, O_CLOEXEC) != 0) {
        throwSystemError("pipe2");
    }
    UniqueFd toClientRead(toClient[0]);
    impl_->out.reset(toClient[1]);
    if (::pipe2(toHost, O_CLOEXEC) != 0) {
        throwSystemError("pipe2");
    }
    impl_->in.reset(toHost[0]);
    UniqueFd toHostWrite(toHost[1]);

    impl_->clientIn = moveTo(std::move(toClientRead), clientReadFd(id.clientId));
    impl_->clientOut = moveTo(std::move(toHostWrite), clientWriteFd(id.clientId));
}

Conn::~Conn() = default;

bool Conn::Read(void* buf, std::size_t count)
{
    if (impl_->waitingForClient() && !fdio::waitReadable(impl_->in.get(), kIpcTimeout)) {
        return false;
    }
    if (!fdio::readExact(impl_->in.get(), buf, count)) {
        return false;
    }
    // Data from the client means it has already opened both of its ends
    impl_->clientIn.reset();
    impl_->clientOut.reset();
    return true;
}

bool Conn::Write(const void* buf, std::size_t count)
{
    return fdio::writeExact(impl_->out.get(), buf, count);
}

const char* Conn::typeCode()
{
    return "pipe";
}

}
