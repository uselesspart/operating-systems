#include "ipc/conn_pipe.hpp"

#include "common/deadline.hpp"
#include "common/named_semaphore.hpp"
#include "common/unique_fd.hpp"
#include "ipc/synchronized_stream.hpp"

#include <array>
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
        throw std::invalid_argument("Invalid pipe session identity");
    }
    return "lab2-pipe-" + std::to_string(geteuid()) + "-" + std::to_string(hostPid) + "-" +
           std::to_string(clientId);
}

class SessionFiles {
public:
    SessionFiles(pid_t hostPid, std::uint32_t clientId)
        : name_(sessionName(hostPid, clientId)), directory("/tmp/" + name_),
          path(directory + "/transfer.sock"), incoming("/" + name_ + "-c2h"),
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
    common::UniqueFd fd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
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

using PipeEnds = std::array<common::UniqueFd, 2>;

PipeEnds newPipe() {
    int descriptors[2]{};
    // pipe2 атомарно устанавливает флаги: другой поток не увидит временно
    // наследуемые дескрипторы при exec, а операции I/O не будут блокирующими.
    if (pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) == -1) {
        fail(errno, "pipe2");
    }
    return {common::UniqueFd(descriptors[0]), common::UniqueFd(descriptors[1])};
}

void sendPipeEnds(int socketFd, int reader, int writer, const common::Deadline& deadline) {
    const int descriptors[]{reader, writer};
    unsigned char marker = 1;
    iovec payload{&marker, sizeof(marker)};
    alignas(cmsghdr) unsigned char control[CMSG_SPACE(sizeof(descriptors))]{};
    msghdr message{};
    message.msg_iov = &payload;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    auto* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(descriptors));
    std::memcpy(CMSG_DATA(header), descriptors, sizeof(descriptors));
    for (;;) {
        waitReady(socketFd, POLLOUT, deadline);
        const auto sent = sendmsg(socketFd, &message, MSG_NOSIGNAL);
        if (sent == 1) {
            return;
        }
        if (sent >= 0) {
            fail(EPROTO, "Incomplete descriptor transfer");
        }
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            fail(errno, "sendmsg(SCM_RIGHTS)");
        }
    }
}

void validatePipeEnd(int fd, int access) {
    struct stat info{};
    if (fstat(fd, &info) == -1) {
        fail(errno, "fstat(pipe)");
    }
    const int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        fail(errno, "fcntl(F_GETFL)");
    }
    if (!S_ISFIFO(info.st_mode) || (flags & O_ACCMODE) != access || !(flags & O_NONBLOCK)) {
        fail(EPROTO, "Unexpected pipe descriptor");
    }
}

PipeEnds receivePipeEnds(int socketFd, const common::Deadline& deadline) {
    for (;;) {
        waitReady(socketFd, POLLIN, deadline);
        unsigned char marker{};
        iovec payload{&marker, sizeof(marker)};
        // Больший буфер позволяет принять и закрыть лишние дескрипторы при ошибке.
        constexpr std::size_t maxDescriptors = 8;
        alignas(cmsghdr) unsigned char control[CMSG_SPACE(maxDescriptors * sizeof(int))]{};
        msghdr message{};
        message.msg_iov = &payload;
        message.msg_iovlen = 1;
        message.msg_control = control;
        message.msg_controllen = sizeof(control);
        const auto received = recvmsg(socketFd, &message, MSG_CMSG_CLOEXEC);
        if (received == -1) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            fail(errno, "recvmsg(SCM_RIGHTS)");
        }
        std::array<common::UniqueFd, maxDescriptors> descriptors;
        std::size_t count{};
        bool valid = true;
        // Сначала принимаем владение всеми полученными fd, даже если пакет неверен.
        // При MSG_CTRUNC ядро закрывает те fd, которые не поместились в буфер.
        for (auto* header = CMSG_FIRSTHDR(&message); header != nullptr;
             header = CMSG_NXTHDR(&message, header)) {
            if (header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
                header->cmsg_len < CMSG_LEN(0)) {
                valid = false;
                continue;
            }
            const auto bytes = header->cmsg_len - CMSG_LEN(0);
            if (bytes % sizeof(int) != 0) {
                valid = false;
            }
            for (std::size_t offset = 0; offset + sizeof(int) <= bytes; offset += sizeof(int)) {
                int fd{};
                std::memcpy(&fd, CMSG_DATA(header) + offset, sizeof(fd));
                common::UniqueFd owned(fd);
                if (count == descriptors.size()) {
                    valid = false;
                } else {
                    descriptors[count++] = std::move(owned);
                }
            }
        }
        if (received == 0) {
            fail(ECONNRESET, "Descriptor transfer closed");
        }
        if (!valid || received != 1 || marker != 1 || count != 2 ||
            (message.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
            fail(EPROTO, "Invalid descriptor transfer");
        }
        validatePipeEnd(descriptors[0].get(), O_RDONLY);
        validatePipeEnd(descriptors[1].get(), O_WRONLY);
        return {std::move(descriptors[0]), std::move(descriptors[1])};
    }
}

} // namespace

struct PipeConnection::Impl {
    std::unique_ptr<SessionFiles> files;
    common::UniqueFd listener;
    PipeEnds hostToClient;
    PipeEnds clientToHost;
    std::unique_ptr<common::NamedSemaphore> incoming;
    std::unique_ptr<common::NamedSemaphore> outgoing;
    std::unique_ptr<SynchronizedStream> stream;
    std::chrono::milliseconds timeout{};
    pid_t expectedPid{};
};

PipeConnection::PipeConnection(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {
}
PipeConnection::~PipeConnection() = default;

std::unique_ptr<PipeConnection> PipeConnection::prepareHost(std::uint32_t clientId, pid_t clientPid,
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
    impl->hostToClient = newPipe();
    impl->clientToHost = newPipe();
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
    return std::unique_ptr<PipeConnection>(new PipeConnection(std::move(impl)));
}

std::unique_ptr<PipeConnection> PipeConnection::connectClient(pid_t hostPid, std::uint32_t clientId,
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
    auto ends = receivePipeEnds(fd.get(), deadline);
    fd.reset();
    impl->stream = std::make_unique<SynchronizedStream>(
        std::move(ends[0]), std::move(ends[1]), std::move(incoming), std::move(outgoing), timeout);
    return std::unique_ptr<PipeConnection>(new PipeConnection(std::move(impl)));
}

void PipeConnection::completeHost() {
    if (!impl_ || !impl_->listener || impl_->stream) {
        throw std::logic_error("No pending host pipe connection");
    }
    try {
        const common::Deadline deadline(impl_->timeout);
        auto fd = acceptPeer(impl_->listener.get(), impl_->expectedPid, deadline);
        sendPipeEnds(fd.get(), impl_->hostToClient[0].get(), impl_->clientToHost[1].get(),
                     deadline);
        // После передачи клиентских концов хост не должен удерживать их открытыми:
        // иначе EOF и EPIPE не обнаружат завершение противоположной стороны.
        impl_->hostToClient[0].reset();
        impl_->clientToHost[1].reset();
        fd.reset();
        impl_->stream = std::make_unique<SynchronizedStream>(
            std::move(impl_->clientToHost[0]), std::move(impl_->hostToClient[1]),
            std::move(*impl_->incoming), std::move(*impl_->outgoing), impl_->timeout);
        impl_->incoming.reset();
        impl_->outgoing.reset();
        impl_->listener.reset();
    } catch (...) {
        impl_.reset();
        throw;
    }
}

bool PipeConnection::isConnected() const noexcept {
    return impl_ && impl_->stream && impl_->stream->isConnected();
}
void PipeConnection::requireConnected() const {
    if (!isConnected()) {
        throw std::logic_error("Pipe connection is not open");
    }
}
void PipeConnection::read(void* buffer, std::size_t count) {
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
void PipeConnection::write(const void* buffer, std::size_t count) {
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
    return "pipe";
}

std::unique_ptr<IHostConnection> prepareHostConnection(std::uint32_t clientId, pid_t clientPid) {
    return PipeConnection::prepareHost(clientId, clientPid);
}

std::unique_ptr<IConnection> connectClientConnection(pid_t hostPid, std::uint32_t clientId) {
    return PipeConnection::connectClient(hostPid, clientId);
}

} // namespace lab2::ipc
