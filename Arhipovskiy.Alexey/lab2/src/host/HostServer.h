#ifndef CHAT_HOST_SERVER_H
#define CHAT_HOST_SERVER_H

#include "chat/ChatRoom.h"
#include "chat/Protocol.h"
#include "host/HostInterfaces.h"
#include "ipc/RuntimeDir.h"
#include "ipc/Signals.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <sys/types.h>

namespace chat {

struct HostOptions {
    std::chrono::milliseconds idleLimit = kDefaultIdleLimit;
};

// Accepts clients (signal handshake), runs one thread per client and relays chat messages
class HostServer : public HostController {
public:
    explicit HostServer(HostListener& listener, HostOptions options = {});
    ~HostServer() override;

    HostServer(const HostServer&) = delete;
    HostServer& operator=(const HostServer&) = delete;

    // Throws std::exception if the runtime directory cannot be created
    void start();
    void stop();

    void sendMessage(int to, const std::string& text) override;

private:
    struct Session;

    void handleHandshake(pid_t pid);
    void serve(Session& session);
    std::optional<protocol::Request> awaitRequest(Session& session, std::string& failure);
    bool reply(Session& session, protocol::Response response);
    void relay(Session& session, const std::vector<OutgoingMessage>& outgoing);
    void finish(Session& session, bool joined, const std::string& reason);
    void reapFinishedLocked();
    void notifyParticipants();

    HostListener& listener_;
    const HostOptions options_;
    ChatRoom room_;
    std::unique_ptr<RuntimeDir> runtimeDir_;
    SignalListener signals_;

    std::mutex sessionsMutex_;
    std::map<int, std::unique_ptr<Session>> sessions_;
    int nextClientId_ = 1;
    std::atomic<bool> stopping_{false};
    bool started_ = false;
};

}

#endif // CHAT_HOST_SERVER_H
