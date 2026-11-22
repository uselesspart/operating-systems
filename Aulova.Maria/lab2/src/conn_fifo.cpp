#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>

#include "connection.h"

namespace {

void closeFd(int& fd)
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

class FifoConnection : public Connection {
public:
    FifoConnection()
        : m_h2c("/tmp/os_lab2_" + std::to_string(getpid()) + "_h2c"),
          m_c2h("/tmp/os_lab2_" + std::to_string(getpid()) + "_c2h")
    {
        std::signal(SIGPIPE, SIG_IGN);
    }

    ~FifoConnection() override
    {
        closeFd(m_in);
        closeFd(m_out);
        if (isOwner()) {
            unlink(m_h2c.c_str());
            unlink(m_c2h.c_str());
        }
    }

    int readFd() const override { return m_in; }

private:
    bool hostCreate() override
    {
        for (const std::string* path : {&m_h2c, &m_c2h}) {
            if (mkfifo(path->c_str(), 0600) != 0) {
                setError(errnoMessage("не удалось создать FIFO " + *path));
                return false;
            }
        }
        return openFifo(m_in, m_c2h, O_RDONLY);
    }

    bool clientOpen() override
    {
        return openFifo(m_in, m_h2c, O_RDONLY) && openFifo(m_out, m_c2h, O_WRONLY);
    }

    bool hostFinish() override { return openFifo(m_out, m_h2c, O_WRONLY); }

    bool openFifo(int& fd, const std::string& path, int access)
    {
        fd = open(path.c_str(), access | O_NONBLOCK);
        if (fd >= 0)
            return true;
        setError(errnoMessage("не удалось открыть FIFO " + path));
        return false;
    }

    SendResult writeFrame(const WireMessage& message) override
    {
        const ssize_t n = ::write(m_out, &message, sizeof message);
        if (n == static_cast<ssize_t>(sizeof message))
            return SendResult::Ok;
        if (n >= 0) {
            setError("в FIFO записан неполный кадр");
            return SendResult::Error;
        }
        if (errno == EAGAIN)
            return SendResult::Busy;
        if (errno == EPIPE)
            return SendResult::Closed;
        setError(errnoMessage("ошибка записи в FIFO"));
        return SendResult::Error;
    }

    RecvResult readFrame(WireMessage& message) override
    {
        while (m_filled < sizeof m_frame) {
            const ssize_t n = ::read(m_in, m_frame + m_filled, sizeof m_frame - m_filled);
            if (n > 0) {
                m_filled += static_cast<std::size_t>(n);
            } else if (n == 0) {
                return RecvResult::Closed;
            } else if (errno == EAGAIN) {
                return RecvResult::Empty;
            } else if (errno != EINTR) {
                setError(errnoMessage("ошибка чтения из FIFO"));
                return RecvResult::Error;
            }
        }
        std::memcpy(&message, m_frame, sizeof m_frame);
        m_filled = 0;
        return RecvResult::Message;
    }

    std::string m_h2c;
    std::string m_c2h;
    int m_in = -1;
    int m_out = -1;
    char m_frame[sizeof(WireMessage)];
    std::size_t m_filled = 0;
};

}

std::unique_ptr<Connection> createConnection()
{
    return std::make_unique<FifoConnection>();
}
