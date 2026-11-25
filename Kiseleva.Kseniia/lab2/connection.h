#pragma once

#include <cstdint>
#include <memory>
#include <semaphore.h>
#include <sys/types.h>

enum class MessageType : std::uint8_t {
    Public,
    Private,
    Exit,
    Heartbeat
};

struct ChatMessage {
    MessageType type{};
    std::uint64_t timestamp_ms{};
    char sender[32]{};
    char recipient[32]{};
    char text[1024]{};
};

enum class ProcessRole {
    Host,
    Client
};

struct SharedSync {
    sem_t host_to_client;
    sem_t client_to_host;
};

class Connection {
public:
    virtual ~Connection() = default;

    virtual bool Read(ChatMessage& message, int timeout_ms = 5000) = 0;
    virtual bool Write(const ChatMessage& message, int timeout_ms = 5000) = 0;
};

using ConnectionPtr = std::unique_ptr<Connection>;

int PrepareConnection(int& host_fd, int& client_fd);

ConnectionPtr CreateConnection(
    ProcessRole role,
    pid_t host_pid,
    SharedSync* sync,
    int fd
);

int RunClient(pid_t host_pid, SharedSync* sync, int fd);