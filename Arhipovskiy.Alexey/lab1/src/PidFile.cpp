#include "PidFile.h"

#include "Logger.h"
#include "UniqueFd.h"

#include <cerrno>
#include <cstring>
#include <fstream>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace diskmon {

PidFile::PidFile(std::string path) : path_(std::move(path)) {}

std::optional<pid_t> PidFile::read() const
{
    std::ifstream file(path_);
    long value = 0;
    if (!(file >> value) || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

bool PidFile::write(pid_t pid) const
{
    UniqueFd fd(::open(path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644));
    if (!fd.valid()) {
        const int err = errno;
        logger::error("Cannot open pid file " + path_ + ": " + std::strerror(err));
        return false;
    }

    const std::string text = std::to_string(pid) + '\n';
    const bool written = ::write(fd.get(), text.data(), text.size()) == static_cast<ssize_t>(text.size());
    const bool closed = ::close(fd.release()) == 0;
    if (!written || !closed) {
        const int err = errno;
        logger::error("Cannot write pid file " + path_ + ": " + std::strerror(err));
        return false;
    }
    return true;
}

void PidFile::removeIfOwnedBy(pid_t pid) const
{
    // A newer instance may have already replaced the file
    if (read() == pid) {
        ::unlink(path_.c_str());
    }
}

}
