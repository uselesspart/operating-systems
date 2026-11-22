#include "connection.hpp"
#include <fcntl.h>
#include <unistd.h>

namespace chat {
class PipeConnection final : public Connection {
    bool owner_;
    int client_;
    std::string path_;
    Fd listener_;
public:
    PipeConnection(int host, int client, bool owner)
        : owner_(owner), client_(client), path_(endpoint_path(host, client)) {}
    ~PipeConnection() override { if (owner_) unlink(path_.c_str()); }
    void prepare() override { if (owner_) listener_ = listener(path_); }
    std::pair<Fd, Fd> open() override {
        if (!owner_) { auto control = connect_socket(path_); return receive_fds(control.get()); }
        auto control = accept_peer(listener_.get(), client_);
        int upstream[2], downstream[2];
        require(pipe2(upstream, O_CLOEXEC | O_NONBLOCK) == 0, "pipe2");
        Fd host_read(upstream[0]), client_write(upstream[1]);
        require(pipe2(downstream, O_CLOEXEC | O_NONBLOCK) == 0, "pipe2");
        Fd client_read(downstream[0]), host_write(downstream[1]);
        send_fds(control.get(), client_read.get(), client_write.get());
        return {std::move(host_read), std::move(host_write)};
    }
};
const char* transport_name() { return "pipe"; }
std::unique_ptr<Connection> make_connection(int host, int client, bool create) {
    return std::make_unique<PipeConnection>(host, client, create);
}
} // namespace chat
