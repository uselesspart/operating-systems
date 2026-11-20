#pragma once

#include "Connection.hpp"
#include "MessageOrder.hpp"
#include <deque>
#include <csignal>
#include <functional>
#include <map>
#include <string>
#include <sys/types.h>

namespace chat {
// Application protocol, independent of widgets and the selected IPC transport.
class ChatSession final {
public:
    ChatSession(Connection& connection, SharedSession& shared, Party role, pid_t peer);
    void submit(const std::string& text, Party recipient);
    void tick();
    void disconnect(const std::string& reason, int childSignal = SIGKILL);
    bool connected() const { return connected_; }
    Party role() const { return role_; }
    pid_t peer() const { return peer_; }
    std::function<void(const Message&)> onMessage;
    std::function<void(const std::string&)> onStatus;
    std::function<void()> onDisconnected;
private:
    struct Pending { Message message; std::int64_t since; };
    void receive(const Message& message);
    void deliverReady();
    void enqueue(const Message& message);
    void checkPeer();
    Connection& connection_;
    SharedSession& shared_;
    Party role_;
    pid_t peer_;
    bool connected_ = true;
    bool childReaped_ = false;
    std::uint64_t expectedDelivery_ = 1;
    std::int64_t lastDeliveryTime_ = 0;
    std::int64_t gapSince_ = 0;
    MessageOrder order_;
    std::deque<Pending> outgoing_;
    std::map<std::uint64_t, std::int64_t> awaitingEcho_;
};
}
