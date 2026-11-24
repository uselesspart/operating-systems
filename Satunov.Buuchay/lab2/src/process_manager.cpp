#include "process_manager.h"

#include <cerrno>
#include <csignal>
#include <ctime>
#include <semaphore.h>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

struct SharedSemaphores::State {
    sem_t host;
    sem_t client;

    sem_t host_write;
    sem_t client_write;
};

namespace {

[[noreturn]] void throw_system_error(const char* operation) {
    throw std::system_error(
        errno, std::generic_category(), operation
    );
}

void post(sem_t* semaphore) {
    if (sem_post(semaphore) == -1) {
        throw_system_error("sem_post");
    }
}

bool wait_with_timeout(sem_t* semaphore) {
    timespec deadline{};

    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
        throw_system_error("clock_gettime");
    }

    deadline.tv_sec += 5;

    for (;;) {
        if (sem_timedwait(semaphore, &deadline) == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ETIMEDOUT) {
            return false;
        }

        throw_system_error("sem_timedwait");
    }
}

} // namespace

SharedSemaphores::SharedSemaphores()
    : state_(nullptr), creator_pid_(getpid()) {

    void* memory = mmap(
        nullptr,
        sizeof(State),
        PROT_READ | PROT_WRITE,
        MAP_SHARED | MAP_ANONYMOUS,
        -1,
        0
    );

    if (memory == MAP_FAILED) {
        throw_system_error("mmap");
    }

    state_ = static_cast<State*>(memory);

    if (sem_init(&state_->host, 1, 0) == -1) {
        const int error = errno;

        munmap(state_, sizeof(State));
        state_ = nullptr;

        throw std::system_error(
            error, std::generic_category(), "sem_init(host)"
        );
    }

    if (sem_init(&state_->client, 1, 0) == -1) {
        const int error = errno;

        sem_destroy(&state_->host);
        munmap(state_, sizeof(State));
        state_ = nullptr;

        throw std::system_error(
            error, std::generic_category(), "sem_init(client)"
        );
    }

    if (sem_init(&state_->host_write, 1, 1) == -1) {
        const int error = errno;

        sem_destroy(&state_->client);
        sem_destroy(&state_->host);
        munmap(state_, sizeof(State));
        state_ = nullptr;

        throw std::system_error(
            error,
            std::generic_category(),
            "sem_init(host_write)"
        );
    }

    if (sem_init(&state_->client_write, 1, 1) == -1) {
        const int error = errno;

        sem_destroy(&state_->host_write);
        sem_destroy(&state_->client);
        sem_destroy(&state_->host);
        munmap(state_, sizeof(State));
        state_ = nullptr;

        throw std::system_error(
            error,
            std::generic_category(),
            "sem_init(client_write)"
        );
    }
}

SharedSemaphores::~SharedSemaphores() {
    if (state_ == nullptr) {
        return;
    }

    if (getpid() == creator_pid_) {
        sem_destroy(&state_->client_write);
        sem_destroy(&state_->host_write);
        sem_destroy(&state_->client);
        sem_destroy(&state_->host);
    }

    munmap(state_, sizeof(State));
}

void SharedSemaphores::notify_host() const {
    post(&state_->host);
}

void SharedSemaphores::notify_client() const {
    post(&state_->client);
}

bool SharedSemaphores::wait_for_host() const {
    return wait_with_timeout(&state_->host);
}

bool SharedSemaphores::wait_for_client() const {
    return wait_with_timeout(&state_->client);
}

ChildProcess::ChildProcess(pid_t pid) noexcept
    : pid_(pid) {}

ChildProcess::ChildProcess(ChildProcess&& other) noexcept
    : pid_(std::exchange(other.pid_, -1)) {}

ChildProcess ChildProcess::spawn(Entry entry, void* context) {
    if (entry == nullptr) {
        throw std::invalid_argument(
            "child entry must not be null"
        );
    }

    const pid_t child = fork();

    if (child == -1) {
        throw_system_error("fork");
    }

    if (child == 0) {
        int result = 127;

        try {
            result = entry(context);
        } catch (...) {
            // Child must not return into the host event loop.
        }

        _exit(result & 0xff);
    }

    return ChildProcess(child);
}

ChildProcess::~ChildProcess() {
    if (pid_ <= 0) {
        return;
    }

    kill(pid_, SIGKILL);

    int status = 0;

    while (waitpid(pid_, &status, 0) == -1 &&
           errno == EINTR) {
    }
}

pid_t ChildProcess::pid() const noexcept {
    return pid_;
}

void ChildProcess::terminate(int signal_number) const {
    if (pid_ > 0 &&
        kill(pid_, signal_number) == -1 &&
        errno != ESRCH) {
        throw_system_error("kill");
    }
}

int ChildProcess::wait() {
    if (pid_ <= 0) {
        throw std::logic_error("child already reaped");
    }

    int status = 0;
    pid_t result;

    do {
        result = waitpid(pid_, &status, 0);
    } while (result == -1 && errno == EINTR);

    if (result == -1) {
        throw_system_error("waitpid");
    }

    pid_ = -1;

    return status;
}

std::optional<int> ChildProcess::try_wait() {
    if (pid_ <= 0) {
        throw std::logic_error("child already reaped");
    }

    int status = 0;
    pid_t result;

    do {
        result = waitpid(pid_, &status, WNOHANG);
    } while (result == -1 && errno == EINTR);

    if (result == 0) {
        return std::nullopt;
    }

    if (result == -1) {
        throw_system_error("waitpid(WNOHANG)");
    }

    pid_ = -1;
    return status;
}

bool SharedSemaphores::wait_for_host_write() const {
    return wait_with_timeout(&state_->host_write);
}

bool SharedSemaphores::wait_for_client_write() const {
    return wait_with_timeout(&state_->client_write);
}

void SharedSemaphores::acknowledge_host_message() const {
    post(&state_->host_write);
}

void SharedSemaphores::acknowledge_client_message() const {
    post(&state_->client_write);
}