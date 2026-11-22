#include "stream.h"

#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

void Stream::send(int fd, const Bytes& packet, const std::atomic_bool& stop, bool socket) {
    if (fd < 0 || packet.empty() || packet.size() > max_packet) {
        throw std::runtime_error("Invalid outgoing packet");
    }
    Bytes frame;
    for (unsigned i = 0; i < 4; ++i) {
        frame.push_back(static_cast<char>(packet.size() >> (i * 8)));
    }
    frame.insert(frame.end(), packet.begin(), packet.end());
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::size_t offset = 0;
    while (offset < frame.size()) {
        check_stop(stop);
        if (Clock::now() >= deadline) {
            throw std::runtime_error("Write timeout");
        }
        ssize_t count = socket ? ::send(fd, frame.data() + offset, frame.size() - offset, MSG_NOSIGNAL)
                           : write(fd, frame.data() + offset, frame.size() - offset);
        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (errno == EPIPE || errno == ECONNRESET) {
                throw ConnectionClosed();
            }
            system_error("Cannot write stream");
        }
        pollfd event{fd, POLLOUT, 0};
        if (poll(&event, 1, remaining_ms(deadline)) < 0 && errno != EINTR) {
            system_error("Cannot wait for stream");
        }
    }
}

ReadResult Stream::receive(int fd, Bytes& packet, const std::atomic_bool& stop, int wait_ms,
                       Deadline* deadline) {
    if (fd < 0) {
        throw std::runtime_error("Not connected");
    }
    auto until = Clock::now() + std::chrono::milliseconds(wait_ms);
    bool polled = false;
    while (true) {
        check_stop(stop);
        if (buffer.size() >= 4) {
            std::uint32_t size = 0;
            for (unsigned i = 0; i < 4; ++i) {
                size |= static_cast<std::uint32_t>(static_cast<unsigned char>(buffer[i])) << (i * 8);
            }
            if (size < header_size || size > max_packet) {
                throw std::runtime_error("Invalid frame size");
            }
            if (buffer.size() >= size + 4) {
                if (deadline) {
                    *deadline = partial_deadline;
                }
                packet.assign(buffer.begin() + 4, buffer.begin() + 4 + size);
                buffer.erase(buffer.begin(), buffer.begin() + 4 + size);
                if (buffer.empty()) {
                    partial_deadline = {};
                }
                return ReadResult::Data;
            }
        }
        if (!buffer.empty() && Clock::now() >= partial_deadline) {
            throw std::runtime_error("Read timeout");
        }
        if (polled && Clock::now() >= until) {
            return ReadResult::NoData;
        }
        int delay = remaining_ms(until);
        if (!buffer.empty()) {
            delay = std::min(delay, remaining_ms(partial_deadline));
        }
        pollfd event{fd, POLLIN, 0};
        int ready = poll(&event, 1, delay);
        polled = true;
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            system_error("Cannot wait for stream");
        }
        if (ready == 0) {
            continue;
        }
        char bytes[4096];
        ssize_t count = read(fd, bytes, sizeof(bytes));
        if (count > 0) {
            if (buffer.empty()) {
                partial_deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
            }
            buffer.insert(buffer.end(), bytes, bytes + count);
        } else if (count == 0) {
            if (!buffer.empty()) {
                throw std::runtime_error("Truncated packet");
            }
            return ReadResult::Closed;
        } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            if (errno == ECONNRESET) {
                if (!buffer.empty()) { throw std::runtime_error("Truncated packet"); }
                return ReadResult::Closed;
            }
            system_error("Cannot read stream");
        }
    }
}
