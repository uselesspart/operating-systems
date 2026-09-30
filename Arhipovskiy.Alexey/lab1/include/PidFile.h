#ifndef DISKMON_PID_FILE_H
#define DISKMON_PID_FILE_H

#include <optional>
#include <string>

#include <sys/types.h>

namespace diskmon {

class PidFile {
public:
    explicit PidFile(std::string path);

    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] std::optional<pid_t> read() const;
    [[nodiscard]] bool write(pid_t pid) const;
    void removeIfOwnedBy(pid_t pid) const;

private:
    std::string path_;
};

}

#endif // DISKMON_PID_FILE_H
