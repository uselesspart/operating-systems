#include "connection.hpp"
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chat {
class FifoConnection final : public Connection {
    bool owner_;
    int client_;
    std::string path_;
    Fd listener_;
public:
    FifoConnection(int host, int client, bool owner)
        : owner_(owner), client_(client), path_(endpoint_path(host, client)) {}
    ~FifoConnection() override {
        if (owner_) { unlink((path_ + ".in").c_str()); unlink((path_ + ".out").c_str()); unlink(path_.c_str()); }
    }
    void prepare() override {
        if (owner_) {
            require(mkfifo((path_ + ".in").c_str(), 0600) == 0, "mkfifo(in)");
            require(mkfifo((path_ + ".out").c_str(), 0600) == 0, "mkfifo(out)");
            listener_ = listener(path_);
        }
    }
    std::pair<Fd, Fd> open() override {
        // The short control connection synchronizes FIFO opening and checks peer credentials.
        // All chat frames travel through the two FIFOs.
        if (owner_) {
            auto control = accept_peer(listener_.get(), client_);
            Fd in(::open((path_ + ".in").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
            require(in.get() >= 0, "open FIFO(in)");
            char byte = 'R';
            wait_fd(control.get(), POLLOUT, Clock::now() + std::chrono::seconds(timeout_seconds));
            require(::write(control.get(), &byte, 1) == 1, "FIFO ready");
            wait_fd(control.get(), POLLIN, Clock::now() + std::chrono::seconds(timeout_seconds));
            require(::read(control.get(), &byte, 1) == 1 && byte == 'R', "FIFO client ready");
            Fd out(::open((path_ + ".out").c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
            require(out.get() >= 0, "open FIFO(out)");
            byte = 'R';
            require(::write(control.get(), &byte, 1) == 1, "FIFO opened");
            return {std::move(in), std::move(out)};
        }
        auto control = connect_socket(path_);
        Fd in(::open((path_ + ".out").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
        require(in.get() >= 0, "open FIFO(out)");
        char byte = 0;
        wait_fd(control.get(), POLLIN, Clock::now() + std::chrono::seconds(timeout_seconds));
        require(::read(control.get(), &byte, 1) == 1 && byte == 'R', "FIFO host ready");
        Fd out(::open((path_ + ".in").c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
        require(out.get() >= 0, "open FIFO(in)");
        require(::write(control.get(), &byte, 1) == 1, "FIFO ready");
        wait_fd(control.get(), POLLIN, Clock::now() + std::chrono::seconds(timeout_seconds));
        require(::read(control.get(), &byte, 1) == 1 && byte == 'R', "FIFO opened");
        return {std::move(in), std::move(out)};
    }
};
const char* transport_name() { return "fifo"; }
std::unique_ptr<Connection> make_connection(int host, int client, bool create) {
    return std::make_unique<FifoConnection>(host, client, create);
}
} // namespace chat
