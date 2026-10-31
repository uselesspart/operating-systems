#pragma once

#include "FileDescriptor.hpp"
#include <string>

namespace reminder {
// Serializes startup and holds the daemon's PID lock for its lifetime.
class PidFile final {
public:
    explicit PidFile(const std::string& path);
    int descriptor() const { return file_.get(); }
    void releaseLaunchLock();
    void writeCurrent();
    void clear();
private:
    FileDescriptor launchLock_;
    FileDescriptor file_;
};
}
