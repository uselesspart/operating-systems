#include "connection.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <limits.h>
#include <new>
#include <semaphore.h>
#include <system_error>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void check(int result, const char* operation)
{
    if (result == -1) {
        throw std::system_error(errno, std::generic_category(), operation);
    }
}

class SharedSignal {
public:
    SharedSignal()
    {
        void* memory = mmap(
            nullptr,
            sizeof(sem_t),
            PROT_READ | PROT_WRITE,
            MAP_SHARED | MAP_ANONYMOUS,
            -1,
            0
        );

        if (memory == MAP_FAILED) {
            throw std::system_error(errno, std::generic_category(), "mmap");
        }

        value_ = new (memory) sem_t{};

        if (sem_init(value_, 1, 0) == -1) {
            const int error = errno;
            munmap(memory, sizeof(sem_t));
            throw std::system_error(error, std::generic_category(), "sem_init");
        }
    }

    ~SharedSignal()
    {
        if (getpid() == owner_ && sem_destroy(value_) == -1) {
            std::perror("sem_destroy");
        }

        munmap(value_, sizeof(sem_t));
    }

    SharedSignal(const SharedSignal&) = delete;
    SharedSignal& operator=(const SharedSignal&) = delete;

    void notify()
    {
        check(sem_post(value_), "sem_post");
    }

    void waitUntil(const timespec& deadline)
    {
        int result;

        do {
            result = sem_timedwait(value_, &deadline);
        } while (result == -1 && errno == EINTR);

        check(result, "sem_timedwait");
    }

private:
    pid_t owner_ = getpid();
    sem_t* value_ = nullptr;
};

int openEnd(int directory, const char* name, int direction)
{
    int fd;

    do {
        fd = openat(directory, name, direction | O_NONBLOCK | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);

    check(fd, name);
    return fd;
}

class FifoConnection final : public Connection {
public:
    FifoConnection() : Connection(PIPE_BUF)
    {
        // До fork создаём только каталог для имён текущей сессии.
        if (mkdtemp(path_) == nullptr) {
            throw std::system_error(errno, std::generic_category(), "mkdtemp");
        }

        directory_ = open(path_, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

        if (directory_ == -1) {
            const int error = errno;
            rmdir(path_);
            throw std::system_error(
                error, std::generic_category(), "open directory"
            );
        }

        std::fprintf(stderr, "FIFO directory: %s\n", path_);
    }

    ~FifoConnection() override
    {
        if (getpid() == owner_) {
            if (madeToClient_ && unlinkat(directory_, "to-client", 0) == -1) {
                std::perror("unlink to-client");
            }

            if (madeToHost_ && unlinkat(directory_, "to-host", 0) == -1) {
                std::perror("unlink to-host");
            }
        }

        ::close(directory_);

        if (getpid() == owner_ && rmdir(path_) == -1) {
            std::perror("rmdir FIFO directory");
        }
    }

    void selectSide(bool isHost) override
    {
        timespec deadline{};
        check(clock_gettime(CLOCK_REALTIME, &deadline), "clock_gettime");
        deadline.tv_sec += 5;

        if (isHost) {
            check(mkfifoat(directory_, "to-host", 0600), "mkfifoat to-host");
            madeToHost_ = true;

            check(mkfifoat(directory_, "to-client", 0600), "mkfifoat to-client");
            madeToClient_ = true;

            host_.input = openEnd(directory_, "to-host", O_RDONLY);
            hostReady_.notify();

            clientReady_.waitUntil(deadline);

            host_.output = openEnd(directory_, "to-client", O_WRONLY);
            hostReady_.notify();
        } else {
            hostReady_.waitUntil(deadline);

            client_.input = openEnd(directory_, "to-client", O_RDONLY);
            client_.output = openEnd(directory_, "to-host", O_WRONLY);
            clientReady_.notify();

            hostReady_.waitUntil(deadline);
        }

        Connection::selectSide(isHost);
    }

private:
    SharedSignal hostReady_;
    SharedSignal clientReady_;

    pid_t owner_ = getpid();
    char path_[sizeof("/tmp/linux-ipc-chat-XXXXXX")] =
        "/tmp/linux-ipc-chat-XXXXXX";

    int directory_ = -1;
    bool madeToHost_ = false;
    bool madeToClient_ = false;
};

} // namespace

std::unique_ptr<Connection> openConnection()
{
    return std::make_unique<FifoConnection>();
}