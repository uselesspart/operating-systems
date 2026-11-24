#pragma once

#include "common/deadline.hpp"

#include <semaphore.h>
#include <string>

namespace lab2::common {

class NamedSemaphore {
public:
    [[nodiscard]] static NamedSemaphore create(std::string name, unsigned int initialValue = 0);
    [[nodiscard]] static NamedSemaphore open(std::string name);

    ~NamedSemaphore() noexcept;
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;
    NamedSemaphore(NamedSemaphore&& other) noexcept;
    NamedSemaphore& operator=(NamedSemaphore&& other) noexcept;

    // false означает только истечение срока; остальные ошибки являются исключениями.
    bool wait(const Deadline& deadline = Deadline{});
    void post();

    // Вызывать после открытия семафора обеими сторонами; открытые ссылки сохраняются.
    void unlink();

private:
    NamedSemaphore(std::string name, sem_t* handle, bool owner) noexcept;
    void cleanup() noexcept;
    void requireOpen() const;

    std::string name_;
    sem_t* handle_{SEM_FAILED};
    bool owner_{};
};

} // namespace lab2::common
