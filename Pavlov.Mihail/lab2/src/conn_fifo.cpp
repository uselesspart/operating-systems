#include "connection.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace wolf {
    namespace {

        class FifoConnection final : public Connection {
        public:
            FifoConnection()
                : toHostName_("/tmp/wolf_lab2_fifo_" + std::to_string(getpid()) + "_to_host"),
                toChildName_("/tmp/wolf_lab2_fifo_" + std::to_string(getpid()) + "_to_child")
            {
            }

            ~FifoConnection() override
            {
                closeFd(incoming_);
                closeFd(outgoing_);

                if (owner_) {
                    (void)::unlink(toHostName_.c_str());
                    (void)::unlink(toChildName_.c_str());
                }
            }

        private:
            static void closeFd(int& fd)
            {
                if (fd != -1) {
                    close(fd);
                    fd = -1;
                }
            }

            static void makeFifo(const std::string& name)
            {
                (void)::unlink(name.c_str());

                if (mkfifo(name.c_str(), 0600) == -1) {
                    throw std::system_error(errno, std::generic_category(),
                        "Не удалось создать именованный канал");
                }
            }

            static int openFifo(const std::string& name, int flags)
            {
                int fd;
                do {
                    fd = ::open(name.c_str(), flags | O_NONBLOCK);
                } while (fd == -1 && errno == EINTR);

                if (fd == -1) {
                    throw std::system_error(errno, std::generic_category(),
                        "Не удалось открыть именованный канал");
                }

                const int current = fcntl(fd, F_GETFL);
                if (current == -1) {
                    const int error = errno;
                    close(fd);
                    throw std::system_error(error, std::generic_category(),
                        "Не удалось получить флаги именованного канала");
                }

                if (fcntl(fd, F_SETFL, current & ~O_NONBLOCK) == -1) {
                    const int error = errno;
                    close(fd);
                    throw std::system_error(error, std::generic_category(),
                        "Не удалось настроить именованный канал");
                }

                return fd;
            }

            void openTransport(Side side) override
            {
                if (side == Side::host) {
                    makeFifo(toHostName_);
                    owner_ = true;
                    makeFifo(toChildName_);

                    incoming_ = openFifo(toHostName_, O_RDONLY);
                    // O_RDWR нужен, чтобы open не блокировался до открытия дочерним процессом.
                    outgoing_ = openFifo(toChildName_, O_RDWR);
                }
                else {
                    incoming_ = openFifo(toChildName_, O_RDONLY);
                    outgoing_ = openFifo(toHostName_, O_WRONLY);
                }
            }

            void receive(void* data, std::size_t size) override
            {
                auto* bytes = static_cast<char*>(data);
                std::size_t total = 0;

                while (total < size) {
                    ssize_t result;
                    do {
                        result = ::read(incoming_, bytes + total, size - total);
                    } while (result == -1 && errno == EINTR);

                    if (result == -1) {
                        throw std::system_error(errno, std::generic_category(),
                            "Ошибка чтения из именованного канала");
                    }
                    if (result == 0) {
                        throw std::runtime_error("Именованный канал закрыт");
                    }

                    total += static_cast<std::size_t>(result);
                }
            }

            void send(const void* data, std::size_t size) override
            {
                const auto* bytes = static_cast<const char*>(data);
                std::size_t total = 0;

                while (total < size) {
                    ssize_t result;
                    do {
                        result = ::write(outgoing_, bytes + total, size - total);
                    } while (result == -1 && errno == EINTR);

                    if (result == -1) {
                        throw std::system_error(errno, std::generic_category(),
                            "Ошибка записи в именованный канал");
                    }

                    total += static_cast<std::size_t>(result);
                }
            }

            std::string toHostName_;
            std::string toChildName_;

            int incoming_ = -1;
            int outgoing_ = -1;

            bool owner_ = false;
        };

    }

    std::unique_ptr<Connection> makeConnection()
    {
        return std::make_unique<FifoConnection>();
    }

}