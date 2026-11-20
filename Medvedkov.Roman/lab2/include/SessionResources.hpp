#pragma once

#include <string>
#include <sys/types.h>

namespace chat {
// A private, unique namespace inherited by the child through fork.
class SessionResources final {
public:
    SessionResources();
    ~SessionResources();
    SessionResources(const SessionResources&) = delete;
    SessionResources& operator=(const SessionResources&) = delete;
    std::string file(const std::string& suffix) const;
    std::string queue(const std::string& suffix) const;
    bool owner() const;
private:
    std::string directory_;
    pid_t owner_;
};
}
