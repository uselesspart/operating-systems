#pragma once

#include "fd.h"

#include <filesystem>
#include <memory>

class PidFile;

class Daemon {
public:
    virtual ~Daemon();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    virtual int run(const std::filesystem::path& config_path) = 0;

protected:
    Daemon();

    int start();
    void ready();
    static bool stopping();
    static bool reload_requested();
    virtual void work() = 0;

private:
    std::unique_ptr<PidFile> pid_file;
    Fd startup_pipe;
};
