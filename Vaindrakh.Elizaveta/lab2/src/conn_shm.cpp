#include "connection.hpp"
#include "ipc_support.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace lab {
class ShmConnection final : public Connection {
public:
    ShmConnection() : owner_(getpid()), name_("/wolf_lab2_" + std::to_string(owner_)) {}
    ~ShmConnection() override {
        if (messages_) munmap(messages_, 2 * sizeof(Message));
        if (getpid() == owner_ && created_) shm_unlink(name_.c_str());
    }
    void afterFork(Role role) override {
        role_ = role;
        if (role == Role::Client) waitSemaphore(sync_.bootstrap());
        // Neither process opens or maps the POSIX shared object before fork.
        int fd = shm_open(name_.c_str(), role == Role::Host ? O_CREAT | O_EXCL | O_RDWR : O_RDWR, 0600);
        if (fd == -1) systemError("shm_open " + name_);
        if (role == Role::Host) created_ = true;
        if (role == Role::Host && ftruncate(fd, 2 * sizeof(Message)) == -1) {
            const int saved = errno;
            close(fd);
            errno = saved;
            systemError("ftruncate shm");
        }
        void* address = mmap(nullptr, 2 * sizeof(Message), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        const int saved = errno;
        close(fd);
        errno = saved;
        if (address == MAP_FAILED) systemError("mmap shm");
        messages_ = static_cast<Message*>(address);
        if (role == Role::Host) postSemaphore(sync_.bootstrap());
    }
    Message Read() override {
        waitSemaphore(sync_.incoming(role_));
        return messages_[role_ == Role::Host ? 0 : 1];
    }
    void Write(const Message& message) override {
        messages_[role_ == Role::Host ? 1 : 0] = message;
        postSemaphore(sync_.outgoing(role_));
    }
private:
    Sync sync_;
    pid_t owner_;
    std::string name_;
    bool created_ = false;
    Message* messages_ = nullptr;
    Role role_ = Role::Host;
};
std::unique_ptr<Connection> createConnection() { return std::make_unique<ShmConnection>(); }
}
