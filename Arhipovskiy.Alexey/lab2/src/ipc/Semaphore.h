#ifndef CHAT_SEMAPHORE_H
#define CHAT_SEMAPHORE_H

#include <chrono>
#include <string>

#include <semaphore.h>

namespace chat {

// POSIX named semaphore (sem_open). The creating side owns the name and unlinks it
class NamedSemaphore {
public:
    static NamedSemaphore create(const std::string& name);
    static NamedSemaphore open(const std::string& name);

    ~NamedSemaphore();
    NamedSemaphore(NamedSemaphore&& other) noexcept;
    NamedSemaphore& operator=(NamedSemaphore&& other) noexcept;
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    bool post();
    // false if the timeout expired
    bool waitFor(std::chrono::milliseconds timeout);

    [[nodiscard]] const std::string& name() const noexcept { return name_; }

private:
    NamedSemaphore(sem_t* semaphore, std::string name, bool owner) noexcept;
    void release() noexcept;

    sem_t* semaphore_ = SEM_FAILED;
    std::string name_;
    bool owner_ = false;
};

}

#endif // CHAT_SEMAPHORE_H
