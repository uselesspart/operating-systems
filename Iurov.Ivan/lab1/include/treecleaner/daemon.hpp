#pragma once

#include <filesystem>
#include <string>

namespace treecleaner {

class PidFile {
public:
    explicit PidFile(const std::filesystem::path& path);
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    void writeCurrentPid();
    void removeIfOwned() noexcept;
    int descriptor() const noexcept { return fd_; }

private:
    std::filesystem::path path_;
    int fd_;
    bool cleaned_ = false;
};

enum class DaemonizeResult { Parent, Child, Error };

// Forks once, creates a new session, changes cwd to /, and redirects standard
// streams to /dev/null. Parent prints the child PID and gets Parent; the child
// gets Child and continues daemon work. Errors are reported via errorMessage.
DaemonizeResult daemonize(std::string& errorMessage);

} // namespace treecleaner
