#include "link/Names.hpp"

namespace chatlab::names {

namespace {

constexpr const char* kStem = "lab2chat-";

std::string slotPart(const ConnKey& key) {
    return "c" + std::to_string(key.slot);
}

}

std::string workDir(pid_t host) {
    return "/tmp/" + std::string(kStem) + std::to_string(host);
}

std::string fifoPath(const ConnKey& key, const char* way) {
    return workDir(key.host) + "/" + slotPart(key) + "." + way + ".fifo";
}

std::string socketPath(const ConnKey& key) {
    return workDir(key.host) + "/" + slotPart(key) + ".sock";
}

std::string semaphorePrefix(pid_t host) {
    return "/" + std::string(kStem) + std::to_string(host) + "-";
}

std::string semaphoreName(const ConnKey& key, const char* way) {
    return semaphorePrefix(key.host) + slotPart(key) + "-" + way;
}

}
