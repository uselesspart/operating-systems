#include "ipc/conn_fifo.hpp"

#include "common/deadline.hpp"
#include "common/named_semaphore.hpp"
#include "common/unique_fd.hpp"
#include "ipc/synchronized_stream.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

namespace lab2::ipc {
namespace {

[[noreturn]] void fail(int error, const char* operation) {
    throw std::system_error(error, std::generic_category(), operation);
}

std::string sessionName(pid_t hostPid, std::uint32_t clientId) {
    if (hostPid <= 0 || clientId == 0 || clientId > static_cast<std::uint32_t>(INT_MAX)) {
        throw std::invalid_argument("Invalid FIFO session identity");
    }
    return "lab2-fifo-" + std::to_string(geteuid()) + "-" + std::to_string(hostPid) + "-" +
           std::to_string(clientId);
}

class SessionFiles {
public:
    SessionFiles(pid_t hostPid, std::uint32_t clientId)
        : name_(sessionName(hostPid, clientId)), directory("/tmp/" + name_),
          hostToClient(directory + "/h2c"), clientToHost(directory + "/c2h"),
          hostToClientSemaphore("/" + name_ + "-h2c"), clientToHostSemaphore("/" + name_ + "-c2h") {
    }

    SessionFiles(const SessionFiles&) = delete;
    SessionFiles& operator=(const SessionFiles&) = delete;

    ~SessionFiles() noexcept {
        const int savedErrno = errno;
        if (ownsHostToClient_) {
            static_cast<void>(unlink(hostToClient.c_str()));
        }
        if (ownsClientToHost_) {
            static_cast<void>(unlink(clientToHost.c_str()));
        }
        if (ownsDirectory_) {
            static_cast<void>(rmdir(directory.c_str()));
        }
        errno = savedErrno;
    }

    void create() {
        // O_EXCL-подобная семантика mkdir: чужая или оставшаяся после сбоя
        // сессия не удаляется и не переиспользуется автоматически.
        if (mkdir(directory.c_str(), 0700) == -1) {
            fail(errno, "mkdir(FIFO session)");
        }
        ownsDirectory_ = true;
        if (mkfifo(hostToClient.c_str(), 0600) == -1) {
            fail(errno, "mkfifo(host to client)");
        }
        ownsHostToClient_ = true;
        if (mkfifo(clientToHost.c_str(), 0600) == -1) {
            fail(errno, "mkfifo(client to host)");
        }
        ownsClientToHost_ = true;
    }

    void validateDirectory() const {
        struct stat info{};
        if (lstat(directory.c_str(), &info) == -1) {
            fail(errno, "lstat(FIFO session)");
        }
        if (!S_ISDIR(info.st_mode) || info.st_uid != geteuid() ||
            (info.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
            fail(EACCES, "Invalid FIFO session directory");
        }
    }

    [[nodiscard]] bool isOwner() const noexcept {
        return ownsDirectory_;
    }

private:
    const std::string name_;

public:
    const std::string directory;
    const std::string hostToClient;
    const std::string clientToHost;
    const std::string hostToClientSemaphore;
    const std::string clientToHostSemaphore;

private:
    bool ownsDirectory_{};
    bool ownsHostToClient_{};
    bool ownsClientToHost_{};
};

common::UniqueFd openFifo(const std::string& path, int access, const common::Deadline& deadline) {
    for (;;) {
        const int fd = open(path.c_str(), access | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
        if (fd >= 0) {
            common::UniqueFd result(fd);
            struct stat info{};
            if (fstat(fd, &info) == -1) {
                fail(errno, "fstat(FIFO)");
            }
            if (!S_ISFIFO(info.st_mode) || info.st_uid != geteuid()) {
                fail(EINVAL, "Invalid FIFO endpoint");
            }
            return result;
        }

        const int error = errno;
        if (error != EINTR && !(access == O_WRONLY && error == ENXIO)) {
            fail(error, "open(FIFO)");
        }
        const int remaining = deadline.remainingMilliseconds();
        if (remaining == 0) {
            fail(ETIMEDOUT, "open(FIFO)");
        }
        if (error == ENXIO) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min(remaining, 10)));
        }
    }
}

} // namespace

struct FifoConnection::Impl {
    Impl(std::unique_ptr<SessionFiles> sessionFiles, common::UniqueFd reader,
         common::UniqueFd writer, common::NamedSemaphore incoming, common::NamedSemaphore outgoing,
         std::chrono::milliseconds operationTimeout)
        : files(std::move(sessionFiles)),
          stream(std::move(reader), std::move(writer), std::move(incoming), std::move(outgoing),
                 operationTimeout),
          timeout(operationTimeout) {
    }

    std::unique_ptr<SessionFiles> files;
    SynchronizedStream stream;
    std::chrono::milliseconds timeout;
};

FifoConnection::FifoConnection(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {
}

FifoConnection::~FifoConnection() = default;

std::unique_ptr<FifoConnection> FifoConnection::prepareHost(std::uint32_t clientId,
                                                            std::chrono::milliseconds timeout) {
    const common::Deadline deadline(timeout);
    auto files = std::make_unique<SessionFiles>(getpid(), clientId);
    files->create();
    auto incoming = common::NamedSemaphore::create(files->clientToHostSemaphore);
    auto outgoing = common::NamedSemaphore::create(files->hostToClientSemaphore);
    auto reader = openFifo(files->clientToHost, O_RDONLY, deadline);
    auto impl = std::make_unique<Impl>(std::move(files), std::move(reader), common::UniqueFd{},
                                       std::move(incoming), std::move(outgoing), timeout);
    return std::unique_ptr<FifoConnection>(new FifoConnection(std::move(impl)));
}

std::unique_ptr<FifoConnection> FifoConnection::connectClient(pid_t hostPid, std::uint32_t clientId,
                                                              std::chrono::milliseconds timeout) {
    const common::Deadline deadline(timeout);
    auto files = std::make_unique<SessionFiles>(hostPid, clientId);
    files->validateDirectory();
    auto incoming = common::NamedSemaphore::open(files->hostToClientSemaphore);
    auto outgoing = common::NamedSemaphore::open(files->clientToHostSemaphore);

    // Входной конец хоста открыт до SIGUSR2. Поэтому клиент может открыть
    // свой выход первым, не ожидая, пока хост откроет второй канал на запись.
    auto writer = openFifo(files->clientToHost, O_WRONLY, deadline);
    auto reader = openFifo(files->hostToClient, O_RDONLY, deadline);
    auto impl = std::make_unique<Impl>(std::move(files), std::move(reader), std::move(writer),
                                       std::move(incoming), std::move(outgoing), timeout);
    return std::unique_ptr<FifoConnection>(new FifoConnection(std::move(impl)));
}

void FifoConnection::completeHost() {
    if (!impl_ || !impl_->files->isOwner() || impl_->stream.isConnected()) {
        throw std::logic_error("No pending host FIFO connection");
    }
    try {
        const common::Deadline deadline(impl_->timeout);
        auto writer = openFifo(impl_->files->hostToClient, O_WRONLY, deadline);
        impl_->stream.attachWriter(std::move(writer));
    } catch (...) {
        impl_.reset();
        throw;
    }
}

bool FifoConnection::isConnected() const noexcept {
    return impl_ && impl_->stream.isConnected();
}

void FifoConnection::requireConnected() const {
    if (!isConnected()) {
        throw std::logic_error("FIFO connection is not open");
    }
}

void FifoConnection::read(void* buffer, std::size_t count) {
    requireConnected();
    try {
        impl_->stream.read(buffer, count);
    } catch (...) {
        if (!impl_->stream.isConnected()) {
            impl_.reset();
        }
        throw;
    }
}

void FifoConnection::write(const void* buffer, std::size_t count) {
    requireConnected();
    try {
        impl_->stream.write(buffer, count);
    } catch (...) {
        if (!impl_->stream.isConnected()) {
            impl_.reset();
        }
        throw;
    }
}

std::string_view transportName() noexcept {
    return "fifo";
}

std::unique_ptr<IHostConnection> prepareHostConnection(std::uint32_t clientId, pid_t clientPid) {
    if (clientPid <= 0) {
        throw std::invalid_argument("Invalid client PID");
    }
    return FifoConnection::prepareHost(clientId);
}

std::unique_ptr<IConnection> connectClientConnection(pid_t hostPid, std::uint32_t clientId) {
    return FifoConnection::connectClient(hostPid, clientId);
}

} // namespace lab2::ipc
