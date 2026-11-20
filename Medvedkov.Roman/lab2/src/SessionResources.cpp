#include "SessionResources.hpp"

#include <cerrno>
#include <cstdlib>
#include <system_error>
#include <unistd.h>

namespace chat {
SessionResources::SessionResources() : owner_(getpid()) {
    char pattern[] = "/tmp/roman-chat-XXXXXX";
    if (!mkdtemp(pattern)) throw std::system_error(errno, std::generic_category(), "mkdtemp");
    directory_ = pattern;
}
SessionResources::~SessionResources() { if (owner()) rmdir(directory_.c_str()); }
bool SessionResources::owner() const { return getpid() == owner_; }
std::string SessionResources::file(const std::string& suffix) const { return directory_ + "/" + suffix; }
std::string SessionResources::queue(const std::string& suffix) const {
    return "/" + directory_.substr(directory_.find_last_of('/') + 1) + "-" + suffix;
}
}
