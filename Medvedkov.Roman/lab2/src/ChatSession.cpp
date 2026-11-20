#include "ChatSession.hpp"

#include <cerrno>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

namespace chat {
ChatSession::ChatSession(Connection& connection, SharedSession& shared, Party role, pid_t peer)
    : connection_(connection), shared_(shared), role_(role), peer_(peer) {}

void ChatSession::enqueue(const Message& message) {
    if (outgoing_.size() >= 64) throw std::runtime_error("Outgoing message queue is full");
    outgoing_.push_back({message, SharedSession::monotonicNs()});
}

void ChatSession::submit(const std::string& text, Party recipient) {
    if (!connected_) throw std::runtime_error("Connection is closed");
    // Validate before allocating a sequence: invalid input must not leave a gap.
    Message::compose(role_, recipient, 1, 1, text);
    if (outgoing_.size() + awaitingEcho_.size() >= 63) throw std::invalid_argument("Wait for pending messages to be delivered");
    try {
        const auto stamp = shared_.reserve(role_);
        auto request = Message::compose(role_, recipient, stamp.sequence, stamp.sentAtNs, text);
        if (role_ == Party::Host) {
            order_.accept(request);
            deliverReady();
        } else {
            enqueue(request);
            awaitingEcho_.emplace(request.sequence, SharedSession::monotonicNs());
        }
    } catch (const std::exception& error) {
        disconnect(error.what());
        throw;
    }
}

void ChatSession::deliverReady() {
    for (const auto& delivery : order_.takeReady()) {
        enqueue(delivery);
        if (delivery.visibleTo(role_) && onMessage) onMessage(delivery);
    }
    if (!order_.hasGap()) gapSince_ = 0;
    else if (!gapSince_) gapSince_ = SharedSession::monotonicNs();
}

void ChatSession::receive(const Message& message) {
    if (role_ == Party::Host) {
        if (message.kind != PacketKind::Request || message.sender != Party::Client) {
            throw std::runtime_error("Host received an invalid request");
        }
        order_.accept(message);
        deliverReady();
    } else {
        if (message.kind != PacketKind::Delivery || message.sequence != expectedDelivery_ ||
            message.sentAtNs <= lastDeliveryTime_) throw std::runtime_error("Invalid delivery order");
        ++expectedDelivery_;
        lastDeliveryTime_ = message.sentAtNs;
        awaitingEcho_.erase(message.sequence);
        if (message.visibleTo(role_) && onMessage) onMessage(message);
    }
}

void ChatSession::checkPeer() {
    if (role_ == Party::Client) {
        if (getppid() != peer_) disconnect("Host has stopped");
        return;
    }
    int status = 0;
    const auto result = waitpid(peer_, &status, WNOHANG);
    if (result == peer_) {
        childReaped_ = true;
        disconnect(WIFSIGNALED(status) ? "Client stopped by signal " + std::to_string(WTERMSIG(status)) : "Client disconnected");
    } else if (result < 0 && errno != EINTR) {
        childReaped_ = true;
        disconnect("Client is no longer available");
    }
}

void ChatSession::tick() {
    if (!connected_) {
        if (role_ == Party::Host && !childReaped_) checkPeer();
        return;
    }
    try {
        checkPeer();
        if (!connected_) return;
        for (int i = 0; i < 16; ++i) {
            auto message = connection_.Read();
            if (!message) break;
            receive(*message);
        }
        const auto now = SharedSession::monotonicNs();
        for (int i = 0; i < 16 && !outgoing_.empty(); ++i) {
            if (now - outgoing_.front().since > SharedSession::TimeoutMs * 1000000LL) {
                throw std::runtime_error("Send queue timeout: 5 seconds");
            }
            if (!connection_.Write(outgoing_.front().message)) break;
            outgoing_.pop_front();
        }
        connection_.CheckProgress();
        for (const auto& item : awaitingEcho_) {
            if (now - item.second > SharedSession::TimeoutMs * 1000000LL) throw std::runtime_error("Host reply timeout: 5 seconds");
        }
        if (gapSince_ && now - gapSince_ > SharedSession::TimeoutMs * 1000000LL) {
            throw std::runtime_error("Missing message sequence: 5 seconds");
        }
        if (role_ == Party::Host && shared_.clientIdle(now)) {
            if (kill(peer_, SIGKILL) < 0 && errno != ESRCH) throw std::runtime_error("Cannot SIGKILL idle client");
            disconnect("Client idle for more than 60 seconds: SIGKILL");
        }
    } catch (const std::exception& error) {
        disconnect(error.what());
    }
}

void ChatSession::disconnect(const std::string& reason, int childSignal) {
    if (!connected_) return;
    connected_ = false;
    if (role_ == Party::Host && !childReaped_) kill(peer_, childSignal);
    std::cerr << (role_ == Party::Host ? "host: " : "client: ") << reason << '\n';
    if (onStatus) onStatus(reason);
    if (onDisconnected) onDisconnected();
}
}
