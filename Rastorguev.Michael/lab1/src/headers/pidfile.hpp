#pragma once

#include <filesystem>
#include <string>
#include <unistd.h>


class PidFile {
public:
    explicit PidFile(std::filesystem::path path);
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    bool terminate_previous(int timeout_ms = 5000);

    bool write_pid();

    void release();

private:
    std::filesystem::path pid_path_;
    bool is_acquired_ = false;

    pid_t read_pid() const;
    static bool is_process_running(pid_t pid);
    static bool is_same_program(pid_t pid);
};
