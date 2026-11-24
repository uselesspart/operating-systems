#include "chat_message.h"
#include "conn_fifo.h"
#include "process_manager.h"

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string(description));
    }
}

std::array<chat::ChatMessage, 2> host_messages() {
    return {{
        {chat::MessageKind::Broadcast, chat::Participant::Host,
         chat::Participant::All, 123, "First message"},
        {chat::MessageKind::Private, chat::Participant::Host,
         chat::Participant::Client, 124, "Second message"}
    }};
}

std::array<chat::ChatMessage, 2> client_messages() {
    return {{
        {chat::MessageKind::Broadcast, chat::Participant::Client,
         chat::Participant::All, 456, "Reply one"},
        {chat::MessageKind::Private, chat::Participant::Client,
         chat::Participant::Host, 457, "Reply two"}
    }};
}

int client_main(void* context) {
    try {
        auto& sync = *static_cast<SharedSemaphores*>(context);

        if (!sync.wait_for_client()) {
            return 1;
        }

        // Child opens FIFO endpoints after fork().
        FifoConnection client(getppid(), false);
        sync.notify_host();

        if (!sync.wait_for_client()) {
            return 2;
        }

        std::vector<std::uint8_t> frame;

        for (const auto& expected : host_messages()) {
            if (!client.read(frame)) {
                return 3;
            }

            const auto incoming = chat::deserialize(frame);

            if (!incoming || *incoming != expected) {
                return 4;
            }
        }

        for (const auto& reply : client_messages()) {
            const auto bytes = chat::serialize(reply);

            if (!bytes || !client.write(*bytes)) {
                return 5;
            }
        }

        sync.notify_host();
        return 0;
    } catch (...) {
        return 9;
    }
}

void test_exchange_and_cleanup() {
    SharedSemaphores sync;
    const auto host_pid = getpid();

    const std::string prefix =
        "/tmp/lab2_fifo_" + std::to_string(getuid()) +
        "_" + std::to_string(host_pid);

    {
        auto child = ChildProcess::spawn(client_main, &sync);

        // Parent creates FIFO files after fork().
        FifoConnection host(host_pid, true);
        sync.notify_client();

        require(
            sync.wait_for_host(),
            "child opens FIFO endpoints"
        );

        for (const auto& request : host_messages()) {
            const auto bytes = chat::serialize(request);

            require(
                bytes && host.write(*bytes),
                "host sends message"
            );
        }

        sync.notify_client();

        require(
            sync.wait_for_host(),
            "client sends replies"
        );

        std::vector<std::uint8_t> frame;

        for (const auto& expected : client_messages()) {
            require(host.read(frame), "host receives reply");

            const auto reply = chat::deserialize(frame);

            require(
                reply && *reply == expected,
                "replies remain framed and ordered"
            );
        }

        require(
            !host.write(std::vector<std::uint8_t>{}),
            "reject empty frame"
        );

        require(
            !host.write(std::vector<std::uint8_t>(1043, 0)),
            "reject oversized frame"
        );

        const int status = child.wait();

        require(
            WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "child exits successfully"
        );
    }

    for (const auto& suffix : {"_h2c", "_c2h"}) {
        errno = 0;

        const int result = access(
            (prefix + suffix).c_str(), F_OK
        );

        require(
            result == -1 && errno == ENOENT,
            "parent removes named pipe after use"
        );
    }
}

void test_read_timeout() {
    const auto host_pid = getpid();

    FifoConnection host(host_pid, true);
    FifoConnection client(host_pid, false);

    std::vector<std::uint8_t> frame;

    require(
        !host.read(frame),
        "idle FIFO read times out"
    );

    require(
        frame.empty(),
        "no partial frame after timeout"
    );
}

void test_partial_frame_rejected() {
    const pid_t host_pid = getpid();

    FifoConnection host(host_pid, true);

    const std::string path =
        "/tmp/lab2_fifo_" +
        std::to_string(getuid()) + "_" +
        std::to_string(host_pid) + "_c2h";

    const int writer = open(
        path.c_str(),
        O_WRONLY | O_NONBLOCK
    );

    require(writer != -1, "open FIFO writer");

    const std::array<std::uint8_t, 2> partial{0, 0};

    const ssize_t written = write(
        writer,
        partial.data(),
        partial.size()
    );

    close(writer);

    require(written == 2, "write partial header");

    std::vector<std::uint8_t> frame;
    bool rejected = false;

    try {
        (void)host.read(frame);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    require(
        rejected,
        "partial FIFO header must cause an error"
    );
}

} // namespace

int main() {
    try {
        test_exchange_and_cleanup();
        test_read_timeout();
        test_partial_frame_rejected();

        std::cout << "FIFO integration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FIFO integration tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}