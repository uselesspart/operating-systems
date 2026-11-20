#pragma once
#include <sys/types.h>

namespace chat {
// Parent-side owner. Reap/stop before destroying shared semaphores and IPC files.
class ChildProcess final {
public:
    explicit ChildProcess(pid_t pid) : pid_(pid) {}
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
private:
    pid_t pid_;
};
}
