#pragma once

#include "fd.h"

#include <filesystem>
#include <string>

class PidFile {
public:
    explicit PidFile(const std::filesystem::path& directory);
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    void stop_previous();
    void save();
    void release_lock();
    int directory_fd() const;
    int lock_fd() const;

private:
    std::string read_pid() const;
    void remove_pid();

    std::filesystem::path directory;
    Fd dir;
    Fd lock;
    std::string record;
    bool owns_pid = false;
};
