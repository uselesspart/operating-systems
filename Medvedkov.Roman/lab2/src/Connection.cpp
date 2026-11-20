#include "Connection.hpp"

#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <stdexcept>
#include <system_error>

namespace chat {
void Connection::Open(Party role) {
    if (role_ != Party::Everyone || (role != Party::Host && role != Party::Client)) {
        throw std::logic_error("Invalid or repeated connection Open");
    }
    if (role == Party::Host) {
        createResources();
        sync_.post(SharedSession::Gate::Created);
        openEndpoint(role);
        sync_.wait(SharedSession::Gate::Opened);
        sync_.startActivityClock();
    } else {
        sync_.wait(SharedSession::Gate::Created);
        openEndpoint(role);
        sync_.post(SharedSession::Gate::Opened);
    }
    role_ = role;
}

bool Connection::Write(const Message& message) {
    if (role_ == Party::Everyone) throw std::logic_error("Connection is not open");
    message.validate();
    const auto empty = role_ == Party::Host ? SharedSession::Gate::ClientEmpty : SharedSession::Gate::HostEmpty;
    const auto full = role_ == Party::Host ? SharedSession::Gate::ClientFull : SharedSession::Gate::HostFull;
    if (!sync_.tryWait(empty)) return false;
    writePacket(message);
    sync_.post(full);
    lastWrite_ = SharedSession::monotonicNs();
    return true;
}

void Connection::CheckProgress() {
    if (!lastWrite_) return;
    const auto empty = role_ == Party::Host ? SharedSession::Gate::ClientEmpty : SharedSession::Gate::HostEmpty;
    if (sync_.tryWait(empty)) {
        sync_.post(empty);
        lastWrite_ = 0;
    } else if (SharedSession::monotonicNs() - lastWrite_ > SharedSession::TimeoutMs * 1000000LL) {
        throw std::runtime_error("Peer did not read the packet within 5 seconds");
    }
}

std::optional<Message> Connection::Read() {
    if (role_ == Party::Everyone) throw std::logic_error("Connection is not open");
    const auto full = role_ == Party::Host ? SharedSession::Gate::HostFull : SharedSession::Gate::ClientFull;
    const auto empty = role_ == Party::Host ? SharedSession::Gate::HostEmpty : SharedSession::Gate::ClientEmpty;
    // No pending request: this is intentional waiting for user input, not an IPC wait.
    if (!sync_.tryWait(full)) return std::nullopt;
    auto packet = readPacket();
    sync_.post(empty);
    packet.validate();
    return packet;
}

std::int64_t Connection::deadline() {
    return SharedSession::monotonicNs() + SharedSession::TimeoutMs * 1000000LL;
}

void Connection::waitFd(int fd, short events, std::int64_t limit) {
    for (;;) {
        const auto left = limit - SharedSession::monotonicNs();
        if (left <= 0) throw std::runtime_error("IPC timeout: 5 seconds");
        pollfd state{fd, events, 0};
        const int count = poll(&state, 1, static_cast<int>((left + 999999) / 1000000));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) throw std::system_error(errno, std::generic_category(), "poll");
        if (count == 0) throw std::runtime_error("IPC timeout: 5 seconds");
        if (state.revents & events) return;
        if (state.revents & (POLLERR | POLLHUP | POLLNVAL)) throw std::runtime_error("Peer closed IPC channel");
    }
}
}
