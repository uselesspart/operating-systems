#pragma once

#include "FileDescriptor.hpp"
#include "InotifyWatcher.hpp"
#include "PidFile.hpp"
#include "Settings.hpp"

#include <string>

#include <sys/types.h>

namespace diskmond {


struct Options {
    
    std::string configFile = "diskmond.conf";
    
    std::string pidFile;
};


class Daemon {
  public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    
    int run(const Options& options);

  private:
    Daemon() = default;
    ~Daemon() = default;

    
    bool prepareLaunch(const Options& options);
    bool readSettings();
    bool replacePreviousInstance();
    bool awaitTermination(pid_t pid) const;

    
    bool bringUp(std::string& error);
    bool prepareSignals(std::string& error);

    bool eventLoop();  // false — цикл прерван ошибкой
    void drainSignals();
    void rereadSettings(pid_t sender);
    void tearDown(bool clean);
    std::string coverage() const;

    
    std::string configPath_;
    Settings settings_;
    PidFile pidFile_;
    InotifyWatcher watcher_;
    
    FileDescriptor signals_;
    bool stopRequested_ = false;
};

}  
