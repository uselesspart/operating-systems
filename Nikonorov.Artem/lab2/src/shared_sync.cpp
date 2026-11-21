#include "shared_sync.hpp"

#include <cerrno>
#include <ctime>
#include <stdexcept>
#include <system_error>
#include <sys/mman.h>

SharedSyncMapping::SharedSyncMapping()
    : sync_(static_cast<SharedSync*>(mmap(nullptr, sizeof(SharedSync), PROT_READ | PROT_WRITE,
                                          MAP_SHARED | MAP_ANONYMOUS, -1, 0))),
      host_to_client_initialized_(false),
      client_to_host_initialized_(false) {
    if (sync_ == MAP_FAILED) {
        sync_ = nullptr;
        throw std::system_error(errno, std::generic_category(), "mmap for shared semaphores failed");
    }

    if (sem_init(&sync_->host_to_client, 1, 0) == -1) {
        const int error = errno;
        munmap(sync_, sizeof(SharedSync));
        sync_ = nullptr;
        throw std::system_error(error, std::generic_category(), "sem_init(host_to_client) failed");
    }
    host_to_client_initialized_ = true;

    if (sem_init(&sync_->client_to_host, 1, 0) == -1) {
        const int error = errno;
        sem_destroy(&sync_->host_to_client);
        munmap(sync_, sizeof(SharedSync));
        sync_ = nullptr;
        host_to_client_initialized_ = false;
        throw std::system_error(error, std::generic_category(), "sem_init(client_to_host) failed");
    }
    client_to_host_initialized_ = true;
}

SharedSyncMapping::~SharedSyncMapping() {
    if (sync_ == nullptr) {
        return;
    }
    if (client_to_host_initialized_) {
        sem_destroy(&sync_->client_to_host);
    }
    if (host_to_client_initialized_) {
        sem_destroy(&sync_->host_to_client);
    }
    munmap(sync_, sizeof(SharedSync));
}

SharedSync* SharedSyncMapping::get() const noexcept {
    return sync_;
}

bool WaitSemaphore(sem_t* semaphore, int timeout_seconds, std::string& error) {
    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
        error = "clock_gettime failed while preparing semaphore timeout";
        return false;
    }
    deadline.tv_sec += timeout_seconds;

    while (sem_timedwait(semaphore, &deadline) == -1) {
        if (errno == EINTR) {
            continue;
        }
        error = (errno == ETIMEDOUT)
            ? "Semaphore wait timed out after " + std::to_string(timeout_seconds) + " seconds"
            : "sem_timedwait failed";
        return false;
    }
    return true;
}

bool PostSemaphore(sem_t* semaphore, std::string& error) {
    if (sem_post(semaphore) == -1) {
        error = "sem_post failed";
        return false;
    }
    return true;
}
