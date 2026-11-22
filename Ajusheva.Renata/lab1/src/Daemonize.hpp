#pragma once

#include "FileDescriptor.hpp"

#include <optional>
#include <string>

#include <sys/types.h>

namespace diskmond {


class StartupChannel {
  public:
    bool open(std::string& error);

    
    void useAsLauncher();
    void useAsDaemon();

    int daemonEnd() const noexcept { return write_.get(); }

    void reportReady(pid_t pid);
    void reportFailure(const std::string& reason);

    
    bool awaitReport(std::string& message);

  private:
    void send(const std::string& text);

    FileDescriptor read_;
    FileDescriptor write_;
};


enum class ProcessRole { Launcher, Daemon };


std::optional<ProcessRole> detachFromTerminal(StartupChannel& channel, std::string& error);


bool settleAsDaemon(int keepFd, std::string& error);

}  