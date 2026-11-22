#include "ipc/LocalSemaphore.h"

#include <sys/mman.h>

#include <cerrno>
#include <cstddef>
#include <ctime>
#include <string>
#include <system_error>

namespace wolf::sem {

namespace {

timespec deadlineAfter(std::chrono::milliseconds timeout) {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    const long long ns = ts.tv_nsec + (timeout.count() % 1000) * 1'000'000LL;
    ts.tv_sec += static_cast<time_t>(timeout.count() / 1000 + ns / 1'000'000'000LL);
    ts.tv_nsec = static_cast<long>(ns % 1'000'000'000LL);
    return ts;
}

[[noreturn]] void fail(const std::string& what) {
    throw std::system_error(errno, std::generic_category(), what);
}

}

sem_t* createShared(int count) {
    const std::size_t bytes = static_cast<std::size_t>(count) * sizeof(sem_t);
    void* memory = ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                          MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) fail("mmap для семафоров");
    auto* base = static_cast<sem_t*>(memory);
    for (int i = 0; i < count; ++i) {
        if (::sem_init(&base[i], 1, 0) != 0) {
            for (int j = 0; j < i; ++j) ::sem_destroy(&base[j]);
            ::munmap(memory, bytes);
            fail("sem_init");
        }
    }
    return base;
}

void destroyShared(sem_t* base, int count) {
    if (base == nullptr) return;
    for (int i = 0; i < count; ++i) ::sem_destroy(&base[i]);
    ::munmap(base, static_cast<std::size_t>(count) * sizeof(sem_t));
}

bool post(sem_t* s) { return s != nullptr && ::sem_post(s) == 0; }

bool wait(sem_t* s, std::chrono::milliseconds timeout) {
    if (s == nullptr) return false;
    const timespec deadline = deadlineAfter(timeout);
    int rc = 0;
    do {
        rc = ::sem_timedwait(s, &deadline);
    } while (rc != 0 && errno == EINTR);
    return rc == 0;
}

}
