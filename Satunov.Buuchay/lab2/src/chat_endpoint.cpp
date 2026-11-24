#include "chat_endpoint.h"
#include "process_manager.h"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace chat {

namespace {

std::uint64_t current_timestamp_ms() {
    const auto now = std::chrono::system_clock::now();

    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        );

    return static_cast<std::uint64_t>(
        milliseconds.count()
    );
}

} // namespace

ChatEndpoint::ChatEndpoint(
    Conn& connection,
    Participant local,
    TimePoint started_at,
    SharedSemaphores* sync
)
    : connection_(connection),
      local_(local),
      peer_(local == Participant::Host
                ? Participant::Client
                : Participant::Host),
      session_(started_at),
      sync_(sync) {

    if (local != Participant::Host &&
        local != Participant::Client) {
        throw std::invalid_argument(
            "endpoint must be Host or Client"
        );
    }
}

bool ChatEndpoint::send(
    MessageKind kind,
    std::string text
) {
    const Participant recipient =
        kind == MessageKind::Broadcast
            ? Participant::All
            : peer_;

    ChatMessage message{
        kind,
        local_,
        recipient,
        current_timestamp_ms(),
        std::move(text)
    };

    const auto bytes = serialize(message);

    if (!bytes) {
        return false;
    }

    if (sync_ != nullptr) {
        const bool permitted =
            local_ == Participant::Host
                ? sync_->wait_for_host_write()
                : sync_->wait_for_client_write();

        if (!permitted) {
            throw std::runtime_error(
                "IPC semaphore wait timed out after 5 seconds"
            );
        }
    }

    if (!connection_.write(*bytes)) {
        if (sync_ != nullptr) {
            throw std::runtime_error(
                "IPC message write failed or timed out"
            );
        }

        return false;
    }

    std::lock_guard lock(mutex_);

    return session_.add_message(
        std::move(message),
        Clock::now()
    );
}

std::optional<ChatMessage> ChatEndpoint::receive() {
    std::vector<std::uint8_t> frame;

    if (!connection_.read(frame)) {
        return std::nullopt;
    }

    auto message = deserialize(frame);

    if (!message) {
        throw std::runtime_error(
            "received an invalid chat message"
        );
    }

    if (message->sender != peer_) {
        throw std::runtime_error(
            "unexpected message sender"
        );
    }

    if (message->kind == MessageKind::Private &&
        message->recipient != local_) {
        throw std::runtime_error(
            "private message addressed to another participant"
        );
    }

    {
        std::lock_guard lock(mutex_);

        if (!session_.add_message(
                *message,
                Clock::now()
            )) {
            throw std::runtime_error(
                "failed to store received message"
            );
        }
    }

    if (sync_ != nullptr) {
        if (peer_ == Participant::Host) {
            sync_->acknowledge_host_message();
        } else {
            sync_->acknowledge_client_message();
        }
    }

    return message;
}

std::vector<ChatMessage>
ChatEndpoint::history() const {
    std::lock_guard lock(mutex_);
    return session_.history();
}

bool ChatEndpoint::client_timed_out(
    TimePoint now
) const {
    if (local_ != Participant::Host) {
        return false;
    }

    std::lock_guard lock(mutex_);
    return session_.client_timed_out(now);
}

} // namespace chat