#include "chat_message.h"
#include "conn_sock.h"
#include "process_manager.h"

#include <array>
#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string(description));
    }
}

std::array<chat::ChatMessage, 2> host_messages() {
    return {{
        {
            chat::MessageKind::Broadcast,
            chat::Participant::Host,
            chat::Participant::All,
            123,
            "Hello from host"
        },
        {
            chat::MessageKind::Private,
            chat::Participant::Host,
            chat::Participant::Client,
            124,
            "Private message"
        }
    }};
}

std::array<chat::ChatMessage, 2> client_messages() {
    return {{
        {
            chat::MessageKind::Broadcast,
            chat::Participant::Client,
            chat::Participant::All,
            456,
            "Hello from client"
        },
        {
            chat::MessageKind::Private,
            chat::Participant::Client,
            chat::Participant::Host,
            457,
            "Private reply"
        }
    }};
}

int client_main(void* context) {
    try {
        auto& sync = *static_cast<SharedSemaphores*>(context);

        if (!sync.wait_for_client()) {
            return 1;
        }

        // Connect after fork() and server initialization.
        SocketConnection client(getppid(), false);
        sync.notify_host();

        if (!sync.wait_for_client()) {
            return 2;
        }

        std::vector<std::uint8_t> frame;

        for (const auto& expected : host_messages()) {
            if (!client.read(frame)) {
                return 3;
            }

            const auto received = chat::deserialize(frame);

            if (!received || *received != expected) {
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
    const pid_t host_pid = getpid();

    const std::string socket_path =
        "/tmp/lab2_sock_" + std::to_string(getuid()) +
        "_" + std::to_string(host_pid) + ".sock";

    {
        auto child = ChildProcess::spawn(client_main, &sync);

        // Server socket is created after fork().
        SocketConnection host(host_pid, true);
        sync.notify_client();

        require(
            sync.wait_for_host(),
            "client must establish connection"
        );

        for (const auto& request : host_messages()) {
            const auto bytes = chat::serialize(request);

            require(
                bytes && host.write(*bytes),
                "host must send message"
            );
        }

        sync.notify_client();

        require(
            sync.wait_for_host(),
            "client must send replies"
        );

        std::vector<std::uint8_t> frame;

        for (const auto& expected : client_messages()) {
            require(
                host.read(frame),
                "host must receive message"
            );

            const auto received = chat::deserialize(frame);

            require(
                received && *received == expected,
                "messages must preserve order and content"
            );
        }

        require(
            !host.write(std::vector<std::uint8_t>{}),
            "empty frame must be rejected"
        );

        require(
            !host.write(std::vector<std::uint8_t>(1043, 0)),
            "oversized frame must be rejected"
        );

        const int status = child.wait();

        require(
            WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "child must finish successfully"
        );
    }

    errno = 0;

    require(
        access(socket_path.c_str(), F_OK) == -1 &&
        errno == ENOENT,
        "server must remove socket file"
    );
}

void test_accept_timeout() {
    SocketConnection host(getpid(), true);

    std::vector<std::uint8_t> frame;

    require(
        !host.read(frame),
        "accept must time out without client"
    );

    require(
        frame.empty(),
        "no data after timeout"
    );
}

void test_maximum_message() {
    SharedSemaphores sync;
    const pid_t host_pid = getpid();

    const auto client_entry = [](void* context) -> int {
        try {
            auto& semaphore =
                *static_cast<SharedSemaphores*>(context);

            if (!semaphore.wait_for_client()) {
                return 1;
            }

            SocketConnection client(getppid(), false);
            semaphore.notify_host();

            std::vector<std::uint8_t> frame;

            if (!client.read(frame)) {
                return 2;
            }

            const auto message = chat::deserialize(frame);

            if (!message ||
                message->text.size() != chat::kMaxTextBytes) {
                return 3;
            }

            return 0;
        } catch (...) {
            return 4;
        }
    };

    auto child = ChildProcess::spawn(client_entry, &sync);

    SocketConnection host(host_pid, true);
    sync.notify_client();

    require(
        sync.wait_for_host(),
        "client connects for maximum message test"
    );

    const chat::ChatMessage message{
        chat::MessageKind::Private,
        chat::Participant::Host,
        chat::Participant::Client,
        999,
        std::string(chat::kMaxTextBytes, 'x')
    };

    const auto bytes = chat::serialize(message);

    require(
        bytes && host.write(*bytes),
        "maximum message must be transmitted"
    );

    const int status = child.wait();

    require(
        WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "client must receive maximum message"
    );
}

    void test_idle_connection_survives() {
    const pid_t host_pid = getpid();

    SocketConnection host(host_pid, true);
    SocketConnection client(host_pid, false);

    std::vector<std::uint8_t> frame;

    require(
        !host.read(frame),
        "idle socket read must time out"
    );

    const chat::ChatMessage message{
        chat::MessageKind::Broadcast,
        chat::Participant::Client,
        chat::Participant::All,
        123,
        "Message after idle timeout"
    };

    const auto bytes = chat::serialize(message);

    require(
        bytes && client.write(*bytes),
        "client can send after idle timeout"
    );

    require(
        host.read(frame),
        "host receives message after idle timeout"
    );

    const auto received = chat::deserialize(frame);

    require(
        received && *received == message,
        "connection preserved the message"
    );
}

    void test_concurrent_read_and_write() {
    SocketConnection host(getpid(), true);
    SocketConnection client(getpid(), false);

    std::atomic<bool> receiver_started{false};
    std::atomic<bool> receiver_got_reply{false};

    std::jthread receiver([&] {
        receiver_started.store(true);

        std::vector<std::uint8_t> frame;

        try {
            receiver_got_reply.store(
                host.read(frame) &&
                frame == std::vector<std::uint8_t>{4, 5, 6}
            );
        } catch (...) {
            receiver_got_reply.store(false);
        }
    });

    while (!receiver_started.load()) {
        std::this_thread::yield();
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50)
    );

    const std::array<std::uint8_t, 3> request{1, 2, 3};

    const auto started =
        std::chrono::steady_clock::now();

    const bool sent = host.write(request);

    const auto elapsed =
        std::chrono::steady_clock::now() - started;

    std::vector<std::uint8_t> received;

    const bool client_got_request =
        client.read(received);

    const std::array<std::uint8_t, 3> reply{4, 5, 6};

    const bool replied = client.write(reply);

    receiver.join();

    require(
        sent && elapsed < std::chrono::seconds(2),
        "sending must not wait for a concurrent read timeout"
    );

    require(
        client_got_request &&
        received == std::vector<std::uint8_t>{1, 2, 3},
        "client receives the concurrent message"
    );

    require(
        replied && receiver_got_reply.load(),
        "host receives the reply concurrently"
    );
}

} // namespace

int main() {
    try {
        test_exchange_and_cleanup();
        test_accept_timeout();
        test_maximum_message();
        test_idle_connection_survives();
        test_concurrent_read_and_write();

        std::cout << "Socket integration tests passed\n";
        return 0;

    } catch (const std::exception& error) {
        std::cerr << "Socket integration tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}