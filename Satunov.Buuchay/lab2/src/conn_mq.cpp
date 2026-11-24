#include "conn_mq.h"

#include "chat_message.h"
#include "transport_name.h"
#include "connection_factory.h"

#include <memory>
#include <array>
#include <cerrno>
#include <ctime>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>

const char* transport_name() noexcept {
    return "mq";
}

namespace {

constexpr long kQueueCapacity = 8;

constexpr std::size_t kMaxFrameBytes =
    chat::kHeaderBytes + chat::kMaxTextBytes;

[[noreturn]] void fail(const char* operation) {
    throw std::system_error(
        errno, std::generic_category(), operation
    );
}

timespec deadline_in_five_seconds() {
    timespec deadline{};

    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
        fail("clock_gettime");
    }

    deadline.tv_sec += 5;
    return deadline;
}

} // namespace

MqConnection::MqConnection(pid_t host_pid, bool create) {
    if (host_pid <= 0) {
        throw std::invalid_argument(
            "host PID must be positive"
        );
    }

    const std::string prefix =
        "/lab2_mq_" + std::to_string(host_pid);

    host_to_client_name_ = prefix + "_h2c";
    client_to_host_name_ = prefix + "_c2h";

    try {
        if (create) {
            mq_attr attributes{};
            attributes.mq_maxmsg = kQueueCapacity;
            attributes.mq_msgsize =
                static_cast<long>(kMaxFrameBytes);

            receive_ = mq_open(
                client_to_host_name_.c_str(),
                O_CREAT | O_EXCL | O_RDONLY,
                0600,
                &attributes
            );

            if (receive_ == static_cast<mqd_t>(-1)) {
                fail("mq_open(c2h)");
            }

            created_client_to_host_ = true;

            send_ = mq_open(
                host_to_client_name_.c_str(),
                O_CREAT | O_EXCL | O_WRONLY | O_NONBLOCK,
                0600,
                &attributes
            );

            if (send_ == static_cast<mqd_t>(-1)) {
                fail("mq_open(h2c)");
            }

            created_host_to_client_ = true;
        } else {
            receive_ = mq_open(
                host_to_client_name_.c_str(),
                O_RDONLY
            );

            if (receive_ == static_cast<mqd_t>(-1)) {
                fail("mq_open(h2c)");
            }

            send_ = mq_open(
                client_to_host_name_.c_str(),
                O_WRONLY | O_NONBLOCK
            );

            if (send_ == static_cast<mqd_t>(-1)) {
                fail("mq_open(c2h)");
            }
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

MqConnection::~MqConnection() {
    cleanup();
}

void MqConnection::cleanup() noexcept {
    if (receive_ != static_cast<mqd_t>(-1)) {
        mq_close(receive_);
        receive_ = static_cast<mqd_t>(-1);
    }

    if (send_ != static_cast<mqd_t>(-1)) {
        mq_close(send_);
        send_ = static_cast<mqd_t>(-1);
    }

    if (created_host_to_client_) {
        mq_unlink(host_to_client_name_.c_str());
        created_host_to_client_ = false;
    }

    if (created_client_to_host_) {
        mq_unlink(client_to_host_name_.c_str());
        created_client_to_host_ = false;
    }
}

bool MqConnection::read(
    std::vector<std::uint8_t>& frame
) {
    frame.clear();

    std::array<std::uint8_t, kMaxFrameBytes> buffer{};
    const timespec deadline = deadline_in_five_seconds();

    for (;;) {
        const ssize_t count = mq_timedreceive(
            receive_,
            reinterpret_cast<char*>(buffer.data()),
            buffer.size(),
            nullptr,
            &deadline
        );

        if (count >= 0) {
            frame.assign(
                buffer.begin(),
                buffer.begin() + count
            );

            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ETIMEDOUT) {
            return false;
        }

        fail("mq_timedreceive");
    }
}

bool MqConnection::write(
    std::span<const std::uint8_t> frame
) {
    if (frame.empty() || frame.size() > kMaxFrameBytes) {
        return false;
    }

    // O_NONBLOCK ensures a full queue never blocks the caller.
    for (;;) {
        if (mq_send(
                send_,
                reinterpret_cast<const char*>(frame.data()),
                frame.size(),
                0
            ) == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN) {
            return false;
        }

        fail("mq_send");
    }
}

std::unique_ptr<Conn>
make_connection(pid_t host_pid, bool create) {
    return std::make_unique<MqConnection>(host_pid, create);
}
