#pragma once
#include "events.hpp"
#include <memory>

namespace application {
struct ServerConfig { std::string name = "host"; };
class Server {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    Server(ServerConfig config, EventSink sink);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;
    void start();
    void post_message(const std::string& text, int recipient = 0);
    void request_stop() noexcept;
    void join();
    bool running() const noexcept;
};
} // namespace application
