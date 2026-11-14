#ifndef CHAT_RUNTIME_DIR_H
#define CHAT_RUNTIME_DIR_H

#include <string>

#include <sys/types.h>

namespace chat {

// Directory for the host's FIFOs and sockets; removed together with its contents on destruction
class RuntimeDir {
public:
    explicit RuntimeDir(pid_t hostPid);
    ~RuntimeDir();

    RuntimeDir(const RuntimeDir&) = delete;
    RuntimeDir& operator=(const RuntimeDir&) = delete;

    [[nodiscard]] const std::string& path() const noexcept { return path_; }

    // Cleans up directories and semaphores of hosts that died without cleanup (e.g. SIGKILL)
    static void removeStale();

private:
    std::string path_;
};

}

#endif // CHAT_RUNTIME_DIR_H
