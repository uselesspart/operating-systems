#include "PidFile.hpp"

#include "FileDescriptor.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace diskmond {

namespace {

constexpr std::size_t kMaxPidDigits = 10;

std::string failure(const std::string& what, int code) {
    return what + ": " + std::strerror(code);
}

}  

PidFile::PidFile(std::string location) : location_(std::move(location)) {}

std::optional<pid_t> PidFile::load() const {
    std::ifstream input(location_);
    std::string firstLine;
    if (!std::getline(input, firstLine)) {
        return std::nullopt;
    }
    const auto first = firstLine.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return std::nullopt;
    }
    const std::string digits = firstLine.substr(first, firstLine.find_last_not_of(" \t\r") - first + 1);
    const bool onlyDigits = std::all_of(digits.begin(), digits.end(),
                                        [](unsigned char symbol) { return std::isdigit(symbol) != 0; });
    if (!onlyDigits || digits.size() > kMaxPidDigits) {
        return std::nullopt;
    }
    const long long number = std::stoll(digits);
    if (number <= 0 || number > std::numeric_limits<pid_t>::max()) {
        return std::nullopt;
    }
    return static_cast<pid_t>(number);
}

bool PidFile::store(pid_t pid, std::string& error) const {
    
    const std::string draft = location_ + ".tmp." + std::to_string(pid);
    ::unlink(draft.c_str());

    FileDescriptor fd(::open(draft.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644));
    if (!fd.isOpen()) {
        const int code = errno;
        error = failure("cannot create pid file " + draft, code);
        return false;
    }

    const std::string line = std::to_string(pid) + "\n";
    const ssize_t stored = ::write(fd.get(), line.c_str(), line.size());
    const int writeError = errno;
    const int closeResult = ::close(fd.release());
    const int closeError = errno;
    if (stored != static_cast<ssize_t>(line.size()) || closeResult != 0) {
        ::unlink(draft.c_str());
        error = failure("cannot write pid file " + draft, closeResult == 0 ? writeError : closeError);
        return false;
    }

    if (::rename(draft.c_str(), location_.c_str()) != 0) {
        const int code = errno;
        ::unlink(draft.c_str());
        error = failure("cannot create pid file " + location_, code);
        return false;
    }
    return true;
}

void PidFile::release(pid_t owner) const {
    if (load() == owner) {
        ::unlink(location_.c_str());
    }
}

std::string defaultPidFileLocation() {
    const uid_t uid = ::geteuid();
    return uid == 0 ? std::string("/run/diskmond.pid") : "/tmp/diskmond-" + std::to_string(uid) + ".pid";
}

}  
