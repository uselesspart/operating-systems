#pragma once

#include "config.hpp"

#include <csignal>
#include <filesystem>
#include <sys/types.h>

class Daemon {
public:
    static Daemon& instance();

    int run(const std::filesystem::path& config_path);

private:
    Daemon() = default;

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    static void signal_handler(int signal_number);

    bool daemonize() const;
    bool stop_previous_instance() const;
    bool is_our_process(pid_t pid) const;
    bool is_process_running(pid_t pid) const;
    bool write_pid_file() const;
    void remove_pid_file() const;
    void run_loop();

    std::filesystem::path config_path_;
    Config config_;

    static volatile std::sig_atomic_t terminate_requested_;
    static volatile std::sig_atomic_t reload_requested_;

    static constexpr const char* kPidFile =
        "/tmp/os_lab1_daemon.pid";
};