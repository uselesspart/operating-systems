#pragma once

#include "Logger.h"

#include <filesystem>
#include <sys/types.h>

namespace disk_monitor {

class PidFile {
public:
    static std::filesystem::path defaultPath();
    static bool processExists(pid_t pid);
    static bool stopPrevious(const std::filesystem::path& path, Logger& logger);

    PidFile() = default;
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    bool acquire(const std::filesystem::path& path, Logger& logger);

private:
    int descriptor_ = -1;
    std::filesystem::path path_;
};

}  // namespace disk_monitor

