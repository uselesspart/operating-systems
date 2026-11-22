#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <stdexcept>

#include "connection.h"

namespace {

void closeFd(int& fd)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

class SockConnection : public Connection {
public:
    SockConnection()
    {
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, m_pair) != 0)
            throw std::runtime_error(errnoMessage("не удалось создать сокеты (socketpair)"));
    }

    ~SockConnection() override
    {
        closeFd(m_pair[0]);
        closeFd(m_pair[1]);
        closeFd(m_fd);
    }

    int readFd() const override { return m_fd; }

private:
    bool hostCreate() override { return useEnd(0); }
    bool clientOpen() override { return useEnd(1); }

    bool useEnd(int own)
    {
        closeFd(m_pair[1 - own]);
        m_fd = m_pair[own];
        m_pair[own] = -1;
        return true;
    }

    SendResult writeFrame(const WireMessage& message) override
    {
        const ssize_t n = ::send(m_fd, &message, sizeof message, MSG_NOSIGNAL);
        if (n == static_cast<ssize_t>(sizeof message))
            return SendResult::Ok;
        if (n >= 0) {
            setError("сокет принял неполный кадр");
            return SendResult::Error;
        }
        if (errno == EAGAIN)
            return SendResult::Busy;
        if (errno == EPIPE || errno == ECONNRESET)
            return SendResult::Closed;
        setError(errnoMessage("ошибка отправки (send)"));
        return SendResult::Error;
    }

    RecvResult readFrame(WireMessage& message) override
    {
        const ssize_t n = ::recv(m_fd, &message, sizeof message, 0);
        if (n == static_cast<ssize_t>(sizeof message))
            return RecvResult::Message;
        if (n == 0)
            return RecvResult::Closed;
        if (n > 0) {
            setError("получен кадр неверного размера");
            return RecvResult::Error;
        }
        if (errno == EAGAIN)
            return RecvResult::Empty;
        if (errno == ECONNRESET)
            return RecvResult::Closed;
        setError(errnoMessage("ошибка приёма (recv)"));
        return RecvResult::Error;
    }

    int m_pair[2] = {-1, -1};
    int m_fd = -1;
};

}

std::unique_ptr<Connection> createConnection()
{
    return std::make_unique<SockConnection>();
}
