#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace chat {
constexpr int timeout_seconds = 5;
constexpr std::size_t max_text = 1024;
using Clock = std::chrono::steady_clock;

struct Message {
    std::uint64_t sequence = 0;
    std::int64_t timestamp = 0;
    int sender = 0;
    int recipient = -1; // -1: everyone; 0: host; positive: client PID.
    std::string text;
};
std::int64_t now_ns();
void validate(const Message& message);
std::string encode(const Message& message);
Message decode(const std::string& bytes);
bool visible_to(const Message& message, int participant);
std::string display(const Message& message);

class Events {
public:
    void push(std::string text);
    std::vector<std::string> take();
private:
    std::mutex mutex_;
    std::vector<std::string> messages_;
};

struct Options {
    bool headless = false;
    bool help = false;
    int host = 0;
};
Options options(int argc, char** argv, bool client);
void block_signals();
std::string session_path(int host);
std::string endpoint_path(int host, int client);

using Send = std::function<void(int, const std::string&)>;
int run_application(int argc, char** argv, bool headless, const std::string& title,
                    Events& events, const Send& send, const std::function<bool()>& alive);

class Server {
public:
    explicit Server(Events& events);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void send(int recipient, const std::string& text);
    bool alive() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Client {
public:
    Client(int host, Events& events);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    void send(int recipient, const std::string& text);
    bool alive() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace chat
