#include "connection.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace wolf {
namespace {

class Pipe {
public:
    Pipe()
    {
        if (pipe2(fds_, O_CLOEXEC | O_NONBLOCK) == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось создать канал");
        }
    }

    ~Pipe()
    {
        closeReader();
        closeWriter();
    }

    void closeReader() { closeFd(fds_[0]); }
    void closeWriter() { closeFd(fds_[1]); }
    int reader() const { return fds_[0]; }
    int writer() const { return fds_[1]; }

private:
    static void closeFd(int& fd)
    {
        if (fd != -1) {
            close(fd);
            fd = -1;
        }
    }

    int fds_[2];
};

class PipeConnection final : public Connection {
private:
    void openTransport(Side side) override
    {
        incoming_ = side == Side::host ? &toHost_ : &toChild_;
        outgoing_ = side == Side::host ? &toChild_ : &toHost_;
        incoming_->closeWriter();
        outgoing_->closeReader();
    }

    void receive(void* data, std::size_t size) override
    {
        ssize_t bytes;
        do {
            bytes = ::read(incoming_->reader(), data, size);
        } while (bytes == -1 && errno == EINTR);
        if (bytes == -1) {
            throw std::system_error(errno, std::generic_category(), "Ошибка чтения из канала");
        }
        if (static_cast<std::size_t>(bytes) != size) {
            throw std::runtime_error("Получено неполное сообщение");
        }
    }

    void send(const void* data, std::size_t size) override
    {
        ssize_t bytes;
        do {
            bytes = ::write(outgoing_->writer(), data, size);
        } while (bytes == -1 && errno == EINTR);
        if (bytes == -1) {
            throw std::system_error(errno, std::generic_category(), "Ошибка записи в канал");
        }
        if (static_cast<std::size_t>(bytes) != size) {
            throw std::runtime_error("Отправлено неполное сообщение");
        }
    }

    Pipe toHost_;
    Pipe toChild_;
    Pipe* incoming_ = nullptr;
    Pipe* outgoing_ = nullptr;
};

}

std::unique_ptr<Connection> makeConnection()
{
    return std::make_unique<PipeConnection>();
}

}
