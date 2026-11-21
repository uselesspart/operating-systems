#pragma once

#include "chat/Hub.hpp"
#include "chat/Packets.hpp"
#include "host/HostEvents.hpp"
#include "link/Signals.hpp"
#include "link/WorkDir.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include <sys/types.h>

namespace chatlab {

struct ServerOptions {
    std::chrono::milliseconds silence = kSilenceLimit;
};

class Server {
  public:
    explicit Server(HostEvents& events, ServerOptions options = {});
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start();
    void stop();
    void say(int to, const std::string& text);

  private:
    struct Session;

    void admit(pid_t guest);
    void serve(Session& session);
    std::optional<packets::Request> awaitRequest(Session& session, std::string& failure);
    bool answer(Session& session, packets::Reply reply);
    void accept(Session& session, const std::vector<Draft>& drafts);
    void finish(Session& session, const std::string& reason);
    void pace();
    void evictSilent();
    void reapLocked();
    void status(const std::string& text);

    HostEvents& events_;
    const ServerOptions options_;
    Hub hub_;
    std::unique_ptr<WorkDir> workDir_;
    sigctl::Watcher watcher_;

    std::mutex sessionsMutex_;
    std::map<int, std::unique_ptr<Session>> sessions_;
    int nextId_ = 1;

    std::atomic<bool> closing_{false};
    std::atomic<bool> running_{false};
    std::thread pacer_;
};

}
