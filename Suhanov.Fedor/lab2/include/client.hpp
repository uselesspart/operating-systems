#pragma once
#include "events.hpp"
#include <memory>

namespace application {
struct ClientConfig { int host_pid = 0; std::string name; };
class Client {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Client(ClientConfig config, EventSink sink);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    void start();
    void post_message(const std::string& text, int recipient = 0);
    void request_stop() noexcept;
    void join();
    bool running() const noexcept;
};
} // namespace application
