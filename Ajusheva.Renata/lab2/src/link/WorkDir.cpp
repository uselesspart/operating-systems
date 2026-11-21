#include "link/WorkDir.hpp"

#include "link/Names.hpp"
#include "util/Errors.hpp"

#include <cerrno>
#include <cstdlib>
#include <functional>
#include <string_view>

#include <dirent.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chatlab {

namespace {

void listDir(const std::string& dir, const std::function<void(const std::string&)>& visit) {
    DIR* handle = ::opendir(dir.c_str());
    if (handle == nullptr) {
        return;
    }
    while (const dirent* entry = ::readdir(handle)) {
        const std::string_view name = entry->d_name;
        if (name != "." && name != "..") {
            visit(std::string(name));
        }
    }
    ::closedir(handle);
}

void wipeDir(const std::string& dir) {
    listDir(dir, [&dir](const std::string& name) { ::unlink((dir + "/" + name).c_str()); });
    ::rmdir(dir.c_str());
}

pid_t ownerPid(const std::string& name, const std::string& stem) {
    if (name.compare(0, stem.size(), stem) != 0) {
        return 0;
    }
    char* end = nullptr;
    const long pid = std::strtol(name.c_str() + stem.size(), &end, 10);
    if (pid <= 0 || end == name.c_str() + stem.size() || (*end != '\0' && *end != '-')) {
        return 0;
    }
    return static_cast<pid_t>(pid);
}

bool isDead(pid_t pid) {
    return ::kill(pid, 0) != 0 && errno == ESRCH;
}

}

WorkDir::WorkDir(pid_t host) : path_(names::workDir(host)) {
    wipeDir(path_);
    if (::mkdir(path_.c_str(), 0700) != 0) {
        raiseErrno("mkdir " + path_);
    }
}

WorkDir::~WorkDir() {
    wipeDir(path_);
}

int WorkDir::sweepLeftovers() {
    const std::string dirStem = "lab2chat-";
    const std::string semStem = "sem.lab2chat-";
    int removed = 0;
    listDir("/tmp", [&](const std::string& name) {
        const pid_t pid = ownerPid(name, dirStem);
        if (pid != 0 && isDead(pid)) {
            wipeDir("/tmp/" + name);
            ++removed;
        }
    });
    listDir("/dev/shm", [&](const std::string& name) {
        const pid_t pid = ownerPid(name, semStem);
        if (pid != 0 && isDead(pid)) {
            ::sem_unlink(("/" + name.substr(4)).c_str());
            ++removed;
        }
    });
    return removed;
}

}
