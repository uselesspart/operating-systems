#pragma once

#include "chat.hpp"
#include <semaphore.h>
#include <sys/types.h>

namespace chat {
class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd();
    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    int get() const { return value_; }
    int release();
private:
    int value_;
};
void require(bool condition, const std::string& operation);
void wait_fd(int fd, short events, Clock::time_point deadline);
void nonblocking(int fd);
Fd listener(const std::string& path);
Fd connect_socket(const std::string& path);
Fd accept_peer(int listener_fd, int expected_pid);
void send_fds(int socket, int read_fd, int write_fd);
std::pair<Fd, Fd> receive_fds(int socket);

// Every conn_*.cpp defines this same interface through a concrete class.
class Connection {
public:
    virtual ~Connection() = default;
    virtual void prepare() = 0;
    virtual std::pair<Fd, Fd> open() = 0;
};
std::unique_ptr<Connection> make_connection(int host, int client, bool create);
const char* transport_name();

class Channel {
public:
    Channel(int host, int client, bool owner);
    ~Channel();
    void prepare();
    void open();
    void write(const Message& message);
    // Idle waiting is deliberate; active I/O and semaphore waits have 5s deadlines.
    bool read(Message& message, const std::atomic<bool>& stop);
private:
    std::unique_ptr<Connection> connection_;
    Fd read_;
    Fd write_;
    bool owner_;
    std::string prefix_;
    sem_t* semaphores_[4] = {SEM_FAILED, SEM_FAILED, SEM_FAILED, SEM_FAILED};
    int read_index_;
    int write_index_;
    std::mutex writer_mutex_;
    void open_semaphores();
};
} // namespace chat
