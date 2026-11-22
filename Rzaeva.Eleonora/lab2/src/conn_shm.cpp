#include "memory_connection.hpp"

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <sys/mman.h>
#include <unistd.h>

namespace wolf {
namespace {

class NamedConnection final : public MemoryConnection {
public:
    ~NamedConnection() override
    {
        if (ownsName_) {
            shm_unlink(name_.c_str());
        }
    }

private:
    void openMemory(Side side) override
    {
        const bool create = side == Side::host;
        const int flags = create ? O_RDWR | O_CREAT | O_EXCL : O_RDWR;
        const int fd = shm_open(name_.c_str(), flags, 0600);
        if (fd == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось открыть объект shm");
        }

        ownsName_ = create;
        try {
            if (create && ftruncate(fd, 2 * capacity) == -1) {
                throw std::system_error(errno, std::generic_category(), "Не удалось задать размер объекта shm");
            }
            mapBuffers(fd);
        } catch (...) {
            close(fd);
            throw;
        }
        close(fd);
    }

    void onConnected() override
    {
        // После отображения в обоих процессах имя больше не нужно.
        if (shm_unlink(name_.c_str()) == -1) {
            throw std::system_error(errno, std::generic_category(), "Не удалось удалить имя объекта shm");
        }
        ownsName_ = false;
    }

    std::string name_ = "/wolf_lab2_" + std::to_string(getpid());
    bool ownsName_ = false;
};

}

std::unique_ptr<Connection> makeConnection()
{
    return std::make_unique<NamedConnection>();
}

}
