#include "memory_connection.hpp"

#include <cerrno>
#include <cstring>
#include <new>
#include <semaphore.h>
#include <stdexcept>
#include <system_error>
#include <sys/mman.h>
#include <time.h>

namespace wolf {
namespace {

constexpr int timeoutSeconds = 5;

[[noreturn]] void throwSystemError(const char* message)
{
    throw std::system_error(errno, std::generic_category(), message);
}

class Semaphore {
public:
    explicit Semaphore(unsigned initial = 0)
    {
        if (sem_init(&value_, 1, initial) == -1) {
            throwSystemError("Не удалось создать семафор");
        }
    }

    ~Semaphore() { sem_destroy(&value_); }

    void post()
    {
        if (sem_post(&value_) == -1) {
            throwSystemError("Не удалось подать сигнал семафору");
        }
    }

    void wait()
    {
        timespec deadline{};
        if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
            throwSystemError("Не удалось получить время");
        }
        deadline.tv_sec += timeoutSeconds;
        while (sem_timedwait(&value_, &deadline) == -1) {
            if (errno == ETIMEDOUT) {
                throwSystemError("Истекло время ожидания семафора (5 секунд)");
            }
            if (errno != EINTR) {
                throwSystemError("Не удалось дождаться сигнала семафора");
            }
        }
    }

private:
    sem_t value_{};
};

void checkSize(std::size_t size)
{
    if (size == 0 || size > Connection::capacity) {
        throw std::invalid_argument("Недопустимый размер сообщения");
    }
}

}

struct Connection::Signals {
    struct Channel {
        Semaphore ready;
        Semaphore free{1};
    };

    struct Shared {
        Semaphore hostReady;
        Semaphore childReady;
        Channel toHost;
        Channel toChild;
    };

    Signals()
    {
        mapping.map(sizeof(Shared));
        shared = new (mapping.data()) Shared;
    }

    ~Signals() { shared->~Shared(); }

    Mapping mapping;
    Shared* shared;
};

Connection::Connection() : signals_(std::make_unique<Signals>())
{
}

Connection::~Connection() = default;

void Connection::open(Side side)
{
    side_ = side;
    // В shm дочерний процесс открывает объект после того, как хост выделил память.
    if (side == Side::child) {
        signals_->shared->hostReady.wait();
        openTransport(side);
        signals_->shared->childReady.post();
    } else {
        openTransport(side);
        signals_->shared->hostReady.post();
        signals_->shared->childReady.wait();
        onConnected();
    }
}

void Connection::read(void* data, std::size_t size)
{
    checkSize(size);
    auto& channel = side_ == Side::host ? signals_->shared->toHost : signals_->shared->toChild;
    channel.ready.wait();
    receive(data, size);
    channel.free.post();
}

void Connection::write(const void* data, std::size_t size)
{
    checkSize(size);
    auto& channel = side_ == Side::host ? signals_->shared->toChild : signals_->shared->toHost;
    // Следующая запись возможна только после чтения предыдущего сообщения.
    channel.free.wait();
    send(data, size);
    channel.ready.post();
}

void Mapping::map(std::size_t size, int fd)
{
    const int flags = fd == -1 ? MAP_SHARED | MAP_ANONYMOUS : MAP_SHARED;
    void* address = mmap(nullptr, size, PROT_READ | PROT_WRITE, flags, fd, 0);
    if (address == MAP_FAILED) {
        throwSystemError("Не удалось отобразить память");
    }
    data_ = address;
    size_ = size;
}

Mapping::~Mapping()
{
    if (data_) {
        munmap(data_, size_);
    }
}

void MemoryConnection::mapBuffers(int fd)
{
    buffers_.map(2 * capacity, fd);
}

void MemoryConnection::openTransport(Side side)
{
    openMemory(side);
    auto* first = static_cast<char*>(buffers_.data());
    auto* second = first + capacity;
    incoming_ = side == Side::host ? second : first;
    outgoing_ = side == Side::host ? first : second;
}

void MemoryConnection::receive(void* data, std::size_t size)
{
    std::memcpy(data, incoming_, size);
}

void MemoryConnection::send(const void* data, std::size_t size)
{
    std::memcpy(outgoing_, data, size);
}

}
