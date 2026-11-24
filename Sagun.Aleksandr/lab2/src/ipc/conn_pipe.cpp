#include "connection.hpp"

#include <cerrno>
#include <fcntl.h>
#include <limits.h>
#include <system_error>
#include <unistd.h>

namespace {

void setNonblocking(int fd)
{
    const int flags = fcntl(fd, F_GETFL);

    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::system_error(errno, std::generic_category(), "fcntl");
    }
}

class PipeConnection final : public Connection {
public:
    PipeConnection() : Connection(PIPE_BUF)
    {
        int ends[2];

        if (pipe(ends) == -1) {
            throw std::system_error(
                errno, std::generic_category(), "pipe to host"
            );
        }

        host_.input = ends[0];
        client_.output = ends[1];

        if (pipe(ends) == -1) {
            throw std::system_error(
                errno, std::generic_category(), "pipe to client"
            );
        }

        client_.input = ends[0];
        host_.output = ends[1];

        setNonblocking(host_.input);
        setNonblocking(host_.output);
        setNonblocking(client_.input);
        setNonblocking(client_.output);
    }
};

} // namespace

std::unique_ptr<Connection> openConnection()
{
    return std::make_unique<PipeConnection>();
}