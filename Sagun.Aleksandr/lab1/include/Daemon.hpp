#include "Config.hpp"
#include "PidFile.hpp"
#include "ReminderTask.hpp"

#include <csignal>
#include <filesystem>

class Daemon
{
private:
    Daemon() = default;
    
    void installSignalHandlers();
    void initializeConfiguration(const std::filesystem::path& configArgument);
    void reloadConfig();
    void mainLoop();
    void daemonize();
    void openLog();
    void sleepInterval();
    void redirectStandardStreams();
    static void signalHandler(int signal);
    void shutdown() noexcept;

    std::filesystem::path configPath_;
    std::filesystem::path pidPath_;
    Config config_;
    PidFile pidFile_;
    ReminderTask reminderTask_;

    static volatile sig_atomic_t terminateRequested_;
    static volatile sig_atomic_t reloadRequested_;

public:
    Daemon(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    static Daemon& getInstance();
    int run(const std::filesystem::path& configArgument);
};


