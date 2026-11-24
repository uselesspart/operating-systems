#include "ipc/conn_sock.hpp"

#include "common/deadline.hpp"
#include "common/named_semaphore.hpp"
#include "common/unique_fd.hpp"
#include "ipc/synchronized_stream.hpp"

#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace lab2::ipc {
namespace {

[[noreturn]] void fail(int error, const char* operation) {
    throw std::system_error(error, std::generic_category(), operation);
}

std::string sessionName(pid_t hostPid, std::uint32_t clientId) {
    if (hostPid <= 0 || clientId == 0 || clientId > static_cast<std::uint32_t>(INT_MAX)) {
        throw std::invalid_argument("Invalid socket session identity");
    }
    return "lab2-sock-" + std::to_string(geteuid()) + "-" + std::to_string(hostPid) + "-" +
           std::to_string(clientId);
}

class SessionFiles {
public:
    SessionFiles(pid_t hostPid, std::uint32_t clientId)
        : name_(sessionName(hostPid, clientId)), directory("/tmp/" + name_),
          path(directory + "/stream.sock"), incoming("/" + name_ + "-c2h"),
          outgoing("/" + name_ + "-h2c") {
    }
    SessionFiles(const SessionFiles&) = delete;
    SessionFiles& operator=(const SessionFiles&) = delete;
    ~SessionFiles() noexcept {
        const int saved = errno;
        if (bound_) {
            static_cast<void>(unlink(path.c_str()));
        }
        if (owner_) {
            static_cast<void>(rmdir(directory.c_str()));
        }
        errno = saved;
    }
    void create() {
        if (mkdir(directory.c_str(), 0700) == -1) {
            fail(errno, "mkdir(socket session)");
        }
        owner_ = true;
    }
    void markBound() noexcept {
        bound_ = true;
    }
    void validate() const {
        struct stat info{};
        if (lstat(directory.c_str(), &info) == -1) {
            fail(errno, "lstat(socket session)");
        }
        if (!S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0077) != 0) {
            fail(EACCES, "Invalid socket session directory");
        }
        if (lstat(path.c_str(), &info) == -1) {
            fail(errno, "lstat(socket endpoint)");
        }
        if (!S_ISSOCK(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0077) != 0) {
            fail(EACCES, "Invalid socket endpoint");
        }
    }

private:
    const std::string name_;

public:
    const std::string directory;
    const std::string path;
    const std::string incoming;
    const std::string outgoing;

private:
    bool owner_{};
    bool bound_{};
};

sockaddr_un address(const std::string& path) {
    sockaddr_un result{};
    result.sun_family = AF_UNIX;
    if (path.size() >= sizeof(result.sun_path)) {
        fail(ENAMETOOLONG, "Unix socket path");
    }
    std::memcpy(result.sun_path, path.c_str(), path.size() + 1);
    return result;
}

common::UniqueFd newSocket() {
    common::UniqueFd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (!fd) {
        fail(errno, "socket");
    }
    return fd;
}

void waitReady(int fd, short events, const common::Deadline& deadline) {
    for (;;) {
        const int remaining = deadline.remainingMilliseconds();
        if (remaining == 0) {
            fail(ETIMEDOUT, "Socket connection deadline");
        }
        pollfd item{fd, events, 0};
        const int result = poll(&item, 1, remaining);
        if (result > 0) {
            if (item.revents & POLLNVAL) {
                fail(EBADF, "poll(socket)");
            }
            return;
        }
        if (result == 0) {
            fail(ETIMEDOUT, "poll(socket)");
        }
        if (errno != EINTR) {
            fail(errno, "poll(socket)");
        }
    }
}

bool peerMatches(int fd, pid_t pid) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) == -1) {
        fail(errno, "getsockopt(SO_PEERCRED)");
    }
    return size == sizeof(credentials) && credentials.pid == pid && credentials.uid == geteuid();
}

common::UniqueFd connectSocket(const std::string& path, const common::Deadline& deadline) {
    const auto endpoint = address(path);
    auto fd = newSocket();
    for (;;) {
        if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)) ==
            0) {
            return fd;
        }
        const int error = errno;
        if (error == EISCONN) {
            return fd;
        }
        if (error == EINPROGRESS || error == EALREADY) {
            waitReady(fd.get(), POLLOUT, deadline);
            int status{};
            socklen_t size = sizeof(status);
            if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &status, &size) == -1) {
                fail(errno, "getsockopt(SO_ERROR)");
            }
            if (status != 0) {
                fail(status, "connect(socket)");
            }
            return fd;
        }
        if (error != EAGAIN && error != EINTR) {
            fail(error, "connect(socket)");
        }
        // Для AF_UNIX EAGAIN означает заполненную очередь listen, а не EINPROGRESS.
        // Повторяем connect с небольшой паузой, не продлевая общий срок.
        if (deadline.remainingMilliseconds() == 0) {
            fail(ETIMEDOUT, "connect(socket)");
        }
        if (error == EAGAIN) {
            static_cast<void>(poll(nullptr, 0, 1));
        }
    }
}

common::UniqueFd acceptPeer(int listener, pid_t pid, const common::Deadline& deadline) {
    for (;;) {
        if (deadline.remainingMilliseconds() == 0) {
            fail(ETIMEDOUT, "accept(socket)");
        }
        common::UniqueFd fd(accept4(listener, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (fd) {
            if (peerMatches(fd.get(), pid)) {
                return fd;
            }
            continue;
        }
        const int error = errno;
        if (error == EINTR) {
            continue;
        }
        if (error != EAGAIN && error != EWOULDBLOCK) {
            fail(error, "accept4(socket)");
        }
        waitReady(listener, POLLIN, deadline);
    }
}

std::unique_ptr<SynchronizedStream> makeStream(common::UniqueFd fd, common::NamedSemaphore incoming,
                                               common::NamedSemaphore outgoing,
                                               std::chrono::milliseconds timeout) {
    // Один двусторонний сокет, две RAII-ссылки: общий поток владеет каждым концом отдельно.
    common::UniqueFd writer(fcntl(fd.get(), F_DUPFD_CLOEXEC, 0));
    if (!writer) {
        fail(errno, "fcntl(F_DUPFD_CLOEXEC)");
    }
    return std::make_unique<SynchronizedStream>(std::move(fd), std::move(writer),
                                                std::move(incoming), std::move(outgoing), timeout);
}

} // namespace

struct SocketConnection::Impl {
    std::unique_ptr<SessionFiles> files;
    common::UniqueFd listener;
    std::unique_ptr<common::NamedSemaphore> incoming;
    std::unique_ptr<common::NamedSemaphore> outgoing;
    std::unique_ptr<SynchronizedStream> stream;
    std::chrono::milliseconds timeout{};
    pid_t expectedPid{};
};

SocketConnection::SocketConnection(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {
}
SocketConnection::~SocketConnection() = default;

std::unique_ptr<SocketConnection> SocketConnection::prepareHost(std::uint32_t clientId,
                                                                pid_t clientPid,
                                                                std::chrono::milliseconds timeout) {
    static_cast<void>(common::Deadline(timeout));
    if (clientPid <= 0) {
        throw std::invalid_argument("Invalid client PID");
    }
    auto impl = std::make_unique<Impl>();
    impl->files = std::make_unique<SessionFiles>(getpid(), clientId);
    impl->files->create();
    impl->incoming = std::make_unique<common::NamedSemaphore>(
        common::NamedSemaphore::create(impl->files->incoming));
    impl->outgoing = std::make_unique<common::NamedSemaphore>(
        common::NamedSemaphore::create(impl->files->outgoing));
    impl->listener = newSocket();
    const auto endpoint = address(impl->files->path);
    if (bind(impl->listener.get(), reinterpret_cast<const sockaddr*>(&endpoint),
             sizeof(endpoint)) == -1) {
        fail(errno, "bind(socket)");
    }
    impl->files->markBound();
    if (chmod(impl->files->path.c_str(), 0600) == -1) {
        fail(errno, "chmod(socket)");
    }
    if (listen(impl->listener.get(), 8) == -1) {
        fail(errno, "listen(socket)");
    }
    impl->timeout = timeout;
    impl->expectedPid = clientPid;
    return std::unique_ptr<SocketConnection>(new SocketConnection(std::move(impl)));
}

std::unique_ptr<SocketConnection>
SocketConnection::connectClient(pid_t hostPid, std::uint32_t clientId,
                                std::chrono::milliseconds timeout) {
    const common::Deadline deadline(timeout);
    auto impl = std::make_unique<Impl>();
    impl->files = std::make_unique<SessionFiles>(hostPid, clientId);
    impl->files->validate();
    auto incoming = common::NamedSemaphore::open(impl->files->outgoing);
    auto outgoing = common::NamedSemaphore::open(impl->files->incoming);
    auto fd = connectSocket(impl->files->path, deadline);
    if (!peerMatches(fd.get(), hostPid)) {
        fail(EACCES, "Unexpected socket host credentials");
    }
    impl->stream = makeStream(std::move(fd), std::move(incoming), std::move(outgoing), timeout);
    return std::unique_ptr<SocketConnection>(new SocketConnection(std::move(impl)));
}

void SocketConnection::completeHost() {
    if (!impl_ || !impl_->listener || impl_->stream) {
        throw std::logic_error("No pending host socket connection");
    }
    try {
        auto fd =
            acceptPeer(impl_->listener.get(), impl_->expectedPid, common::Deadline(impl_->timeout));
        impl_->stream = makeStream(std::move(fd), std::move(*impl_->incoming),
                                   std::move(*impl_->outgoing), impl_->timeout);
        impl_->incoming.reset();
        impl_->outgoing.reset();
        impl_->listener.reset();
    } catch (...) {
        impl_.reset();
        throw;
    }
}

bool SocketConnection::isConnected() const noexcept {
    return impl_ && impl_->stream && impl_->stream->isConnected();
}
void SocketConnection::requireConnected() const {
    if (!isConnected()) {
        throw std::logic_error("Socket connection is not open");
    }
}
void SocketConnection::read(void* buffer, std::size_t count) {
    requireConnected();
    try {
        impl_->stream->read(buffer, count);
    } catch (...) {
        if (!impl_->stream->isConnected()) {
            impl_.reset();
        }
        throw;
    }
}
void SocketConnection::write(const void* buffer, std::size_t count) {
    requireConnected();
    try {
        impl_->stream->write(buffer, count);
    } catch (...) {
        if (!impl_->stream->isConnected()) {
            impl_.reset();
        }
        throw;
    }
}
std::string_view transportName() noexcept {
    return "sock";
}

std::unique_ptr<IHostConnection> prepareHostConnection(std::uint32_t clientId, pid_t clientPid) {
    return SocketConnection::prepareHost(clientId, clientPid);
}

std::unique_ptr<IConnection> connectClientConnection(pid_t hostPid, std::uint32_t clientId) {
    return SocketConnection::connectClient(hostPid, clientId);
}

} // namespace lab2::ipc
