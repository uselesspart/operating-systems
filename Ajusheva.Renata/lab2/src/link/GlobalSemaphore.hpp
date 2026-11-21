#pragma once

#include <chrono>
#include <string>

#include <semaphore.h>

namespace chatlab {

class GlobalSemaphore {
  public:
    static GlobalSemaphore create(const std::string& name);
    static GlobalSemaphore attach(const std::string& name);

    GlobalSemaphore(GlobalSemaphore&& other) noexcept;
    GlobalSemaphore& operator=(GlobalSemaphore&& other) noexcept;
    GlobalSemaphore(const GlobalSemaphore&) = delete;
    GlobalSemaphore& operator=(const GlobalSemaphore&) = delete;
    ~GlobalSemaphore();

    bool post();
    bool waitFor(std::chrono::milliseconds timeout);

    const std::string& name() const noexcept { return name_; }

  private:
    GlobalSemaphore(sem_t* handle, std::string name, bool owner) noexcept;
    void drop() noexcept;

    sem_t* handle_ = SEM_FAILED;
    std::string name_;
    bool owner_ = false;
};

}
