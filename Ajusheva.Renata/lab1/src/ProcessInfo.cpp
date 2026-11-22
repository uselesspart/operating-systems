#include "ProcessInfo.hpp"

#include "PathUtil.hpp"

#include <climits>
#include <fstream>

#include <sys/stat.h>
#include <unistd.h>

namespace diskmond::process {

namespace {

constexpr const char* kDeletedMark = " (deleted)";

std::string procEntry(pid_t pid, const char* entry) {
    return "/proc/" + std::to_string(pid) + "/" + entry;
}

std::optional<std::string> readFirstLine(const std::string& file) {
    std::ifstream input(file);
    std::string line;
    if (!std::getline(input, line)) {
        return std::nullopt;
    }
    return line;
}

}  

bool isAlive(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    struct stat info {};
    if (::stat(("/proc/" + std::to_string(pid)).c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        return false;
    }

    const auto line = readFirstLine(procEntry(pid, "stat"));
    if (!line) {
        return false;
    }
    const auto commEnd = line->rfind(')');
    if (commEnd == std::string::npos || commEnd + 2 >= line->size()) {
        return false;
    }
    const char state = (*line)[commEnd + 2];
    return state != 'Z' && state != 'X';
}

std::optional<std::string> name(pid_t pid) {
    return readFirstLine(procEntry(pid, "comm"));
}

std::optional<std::string> executableName(pid_t pid) {
    char target[PATH_MAX];
    const ssize_t length = ::readlink(procEntry(pid, "exe").c_str(), target, sizeof target - 1);
    if (length <= 0) {
        return std::nullopt;  // процесс другого пользователя: ссылка недоступна
    }
    std::string executable(target, static_cast<std::size_t>(length));
    const std::string mark = kDeletedMark;
    if (executable.size() > mark.size() &&
        executable.compare(executable.size() - mark.size(), mark.size(), mark) == 0) {
        executable.erase(executable.size() - mark.size());
    }
    return path::nameOf(executable);
}

bool isSameProgram(pid_t pid) {
    const auto theirName = name(pid);
    const auto ourName = name(::getpid());
    if (!theirName || !ourName || *theirName != *ourName) {
        return false;
    }

    const auto theirExecutable = executableName(pid);
    const auto ourExecutable = executableName(::getpid());
    if (theirExecutable && ourExecutable) {
        return *theirExecutable == *ourExecutable;
    }
    return true;
}

}  
