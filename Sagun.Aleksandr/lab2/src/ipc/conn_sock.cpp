#include "connection.hpp"

#include <cerrno>
#include <limits>
#include <sys/socket.h>
#include <system_error>

namespace {

class SocketConnection final : public Connection {
public:
    SocketConnection() : Connection(std::numeric_limits<std::size_t>::max())
    {
        int sockets[2];

        if (socketpair(
                AF_UNIX,
                SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC,
                0,
                sockets
            ) == -1) {
            throw std::system_error(
                errno, std::generic_category(), "socketpair"
            );
        }

        host_.input = host_.output = sockets[0];
        client_.input = client_.output = sockets[1];
    }
};

} // namespace

std::unique_ptr<Connection> openConnection()
{
    return std::make_unique<SocketConnection>();
}