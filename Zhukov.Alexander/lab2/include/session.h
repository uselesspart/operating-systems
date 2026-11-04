#pragma once

#include "conn.h"
#include "fd.h"

#include <sys/types.h>

class Session {
public:
    Session(std::unique_ptr<Conn> conn, pid_t host_pid, bool host);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    static void block_signals();
    static std::string path(pid_t pid, const std::string& type);
    static std::string semaphore_name(pid_t pid, const std::string& type, const std::string& suffix);
    void connect(const std::atomic_bool& stop);
    bool writable() const { return conn->writable(); }
    bool accept(const std::atomic_bool& stop);
    Message prepare(Message message);
    Message send(Message message, const std::atomic_bool& stop);
    void forward(const Message& message, const std::atomic_bool& stop);
    void kill_peer();
    ReadResult receive(Message& message, const std::atomic_bool& stop, int wait_ms = poll_ms);
    pid_t peer_pid() const { return peer; }

private:
    void cleanup() noexcept;
    void write_marker(const char* name, const std::string& text);
    std::string read_marker(const char* name);
    void transmit(const Message& message, const std::atomic_bool& stop);

    std::unique_ptr<Conn> conn;
    pid_t host_pid;
    bool host;
    std::string directory;
    Fd dir;
    Fd peer_fd;
    NamedSemaphore slot;
    NamedSemaphore accepted;
    NamedSemaphore to_host;
    NamedSemaphore to_client;
    pid_t peer = 0;
    bool owner = false;
    bool claimed = false;
    bool request_created = false;
    bool connected = false;
    std::uint64_t number = 0;
};
