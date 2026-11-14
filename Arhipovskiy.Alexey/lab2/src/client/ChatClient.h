#ifndef CHAT_CHAT_CLIENT_H
#define CHAT_CHAT_CLIENT_H

#include "chat/Message.h"
#include "chat/Protocol.h"
#include "ipc/Channel.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/types.h>

namespace chat {

// Notifications from the client session; called from its network thread
class ClientListener {
public:
    virtual ~ClientListener() = default;

    virtual void onMessage(const ChatMessage& message) = 0;
    virtual void onNotice(const std::string& notice) = 0;
    virtual void onDisconnected(const std::string& reason, bool lost) = 0;
};

class ClientError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Client side of the chat: handshake, then a request/response cycle every kPollInterval
// that carries queued messages to the host and brings back messages for this client
class ChatClient {
public:
    ChatClient(pid_t hostPid, std::string requestedName, ClientListener& listener);
    ~ChatClient();

    ChatClient(const ChatClient&) = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    // Throws ClientError with a message for the user
    void connect();
    void start();

    void send(int to, const std::string& text);
    // Sends queued messages, says goodbye to the host and waits for the network thread
    void leave();
    void wait();

    [[nodiscard]] bool running() const { return running_; }
    [[nodiscard]] bool lostConnection() const { return lost_; }
    [[nodiscard]] int id() const { return id_; }
    [[nodiscard]] std::string name() const;
    [[nodiscard]] std::vector<Participant> participants() const;

private:
    void run();
    std::optional<protocol::Response> exchange(const protocol::Request& request, std::string& failure);
    void apply(const protocol::Response& response);
    void disconnect(const std::string& reason, bool lost);

    const pid_t hostPid_;
    const std::string requestedName_;
    ClientListener& listener_;
    std::unique_ptr<Channel> channel_;
    int id_ = 0;

    mutable std::mutex mutex_;
    std::condition_variable wakeUp_;
    std::deque<OutgoingMessage> outbox_;
    std::vector<Participant> participants_;
    bool leaveRequested_ = false;

    std::atomic<bool> running_{false};
    std::atomic<bool> lost_{false};
    std::thread thread_;
};

}

#endif // CHAT_CHAT_CLIENT_H
