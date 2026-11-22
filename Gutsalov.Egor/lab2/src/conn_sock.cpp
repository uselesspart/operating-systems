#include "connection.hpp"
#include <fcntl.h>
#include <unistd.h>

namespace chat {
class SocketConnection final : public Connection {
    bool owner_;
    int client_;
    std::string path_;
    Fd listener_;
public:
    SocketConnection(int host, int client, bool owner)
        : owner_(owner), client_(client), path_(endpoint_path(host, client)) {}
    ~SocketConnection() override { if (owner_) unlink(path_.c_str()); }
    void prepare() override { if (owner_) listener_ = listener(path_); }
    std::pair<Fd, Fd> open() override {
        Fd fd = owner_ ? accept_peer(listener_.get(), client_) : connect_socket(path_);
        Fd writer(fcntl(fd.get(), F_DUPFD_CLOEXEC, 0));
        require(writer.get() >= 0, "dup(socket)");
        return {std::move(fd), std::move(writer)};
    }
};
const char* transport_name() { return "sock"; }
std::unique_ptr<Connection> make_connection(int host, int client, bool create) {
    return std::make_unique<SocketConnection>(host, client, create);
}
} // namespace chat
