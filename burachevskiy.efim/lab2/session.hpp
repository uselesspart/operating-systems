#pragma once

#include "conn.hpp"
#include "system.hpp"

#include <array>
#include <memory>
#include <semaphore.h>
#include <sys/types.h>

using Frame = std::array<char, frameSize>;

inline Frame frame(char kind, const std::string& text = {}) {
    if (text.size() >= frameSize - 1) throw std::runtime_error("Сообщение слишком длинное");
    Frame result{};
    result[0] = kind;
    text.copy(result.data() + 1, text.size());
    return result;
}

inline std::string message(const Frame& value) {
    return std::string(value.data() + 1, strnlen(value.data() + 1, value.size() - 1));
}

struct Shared {
    sem_t started;
    sem_t request;
    sem_t reply;
};

void post(sem_t& semaphore);
void wait(sem_t& semaphore);
int runClient(Shared& shared, const std::string& directory, int hostSocket, int clientSocket);

class Session {
    std::string directory_;
    Shared* shared_ = nullptr;
    int initialized_ = 0;
    pid_t client_ = -1;
    std::unique_ptr<Conn> connection_;
    bool waiting_ = false;
    Clock::time_point deadline_;

    void cleanup() noexcept;

public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void send(const Frame& value);
    bool receive(Frame& value);
    bool waiting() const { return waiting_; }
    void stop(int signal) noexcept;
};
