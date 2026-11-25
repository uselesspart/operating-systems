#include "Listener.hpp"
#include "ipc.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace application {
namespace {
Fd open_fifo(const std::string& path) {
    // O_RDWR avoids deadlocking during the signal handshake. Peer liveness is
    // checked by the application because this mode intentionally suppresses EOF.
    const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { system_error("open FIFO " + path); }
    Fd result(fd);
    struct stat info{};
    if (fstat(fd, &info) < 0) { system_error("fstat FIFO"); }
    if (!S_ISFIFO(info.st_mode) || info.st_uid != getuid()) { throw Error("Invalid FIFO " + path); }
    return result;
}
class FifoConnection final : public FramedConnection {
public:
    FifoConnection(std::shared_ptr<ChannelResources> resources, bool host)
        : FramedConnection(resources,
            open_fifo(resources->directory + (host ? "/up" : "/down")),
            open_fifo(resources->directory + (host ? "/down" : "/up")), host) {}
};
class FifoPending final : public PendingConnection {
    std::shared_ptr<ChannelResources> resources_;
public:
    FifoPending(const std::string& root, int pid)
        : resources_(std::make_shared<ChannelResources>(root, pid, true)) {
        for (const auto* name : {"/up", "/down"}) {
            const auto path = resources_->directory + name;
            if (mkfifo(path.c_str(), 0600) < 0) { system_error("mkfifo " + path); }
        }
    }
    std::shared_ptr<IConnection> accept() override {
        return std::make_shared<FifoConnection>(resources_, true);
    }
};
class FifoListener final : public IListener {
    std::string root_;
public:
    explicit FifoListener(std::string root) : root_(std::move(root)) {}
    std::unique_ptr<PendingConnection> prepare(int pid) override {
        return std::make_unique<FifoPending>(root_, pid);
    }
};
} // namespace
std::unique_ptr<IListener> make_listener(const std::string& root) {
    return std::make_unique<FifoListener>(root);
}
std::shared_ptr<IConnection> connect_to_host(const std::string& root, int pid) {
    return std::make_shared<FifoConnection>(std::make_shared<ChannelResources>(root, pid, false), false);
}
} // namespace application
