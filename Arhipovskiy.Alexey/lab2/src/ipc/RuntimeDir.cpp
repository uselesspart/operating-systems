#include "ipc/RuntimeDir.h"

#include "ipc/Names.h"
#include "util/Log.h"
#include "util/SystemError.h"

#include <cerrno>
#include <cstdlib>
#include <memory>
#include <string_view>

#include <dirent.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chat {

namespace {

struct DirCloser {
    void operator()(DIR* dir) const noexcept { closedir(dir); }
};

using DirHandle = std::unique_ptr<DIR, DirCloser>;

template <typename Visitor>
void forEachEntry(const std::string& dir, Visitor visit)
{
    const DirHandle handle(opendir(dir.c_str()));
    if (!handle) {
        return;
    }
    while (const dirent* entry = readdir(handle.get())) {
        const std::string_view name = entry->d_name;
        if (name != "." && name != "..") {
            visit(std::string(name));
        }
    }
}

void removeDirectory(const std::string& dir)
{
    forEachEntry(dir, [&dir](const std::string& name) { ::unlink((dir + "/" + name).c_str()); });
    ::rmdir(dir.c_str());
}

// Parses the pid that follows `prefix` in `name`; 0 if the name does not match
pid_t pidAfterPrefix(const std::string& name, std::string_view prefix)
{
    if (name.compare(0, prefix.size(), prefix) != 0) {
        return 0;
    }
    char* end = nullptr;
    const long pid = std::strtol(name.c_str() + prefix.size(), &end, 10);
    const bool terminated = *end == '\0' || *end == '_';
    return (pid > 0 && terminated) ? static_cast<pid_t>(pid) : 0;
}

bool processGone(pid_t pid)
{
    return ::kill(pid, 0) != 0 && errno == ESRCH;
}

}

RuntimeDir::RuntimeDir(pid_t hostPid) : path_(names::runtimeDir(hostPid))
{
    removeDirectory(path_);
    if (::mkdir(path_.c_str(), 0700) != 0) {
        throwSystemError("mkdir " + path_);
    }
}

RuntimeDir::~RuntimeDir()
{
    removeDirectory(path_);
}

void RuntimeDir::removeStale()
{
    forEachEntry("/tmp", [](const std::string& name) {
        const pid_t pid = pidAfterPrefix(name, "chat_");
        if (pid != 0 && processGone(pid)) {
            removeDirectory("/tmp/" + name);
            log::info("Removed stale runtime directory /tmp/" + name);
        }
    });
    // Named semaphores live in /dev/shm as "sem.<name>"
    constexpr std::string_view kSemaphoreFilePrefix = "sem.";
    forEachEntry("/dev/shm", [&](const std::string& name) {
        const pid_t pid = pidAfterPrefix(name, "sem.chat_");
        if (pid != 0 && processGone(pid)) {
            ::sem_unlink(("/" + name.substr(kSemaphoreFilePrefix.size())).c_str());
        }
    });
}

}
