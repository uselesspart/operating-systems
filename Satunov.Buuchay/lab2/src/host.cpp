
#include "chat_endpoint.h"
#include "chat_window.h"
#include "connection_factory.h"
#include "process_manager.h"
#include "transport_name.h"

#include <QApplication>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <exception>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <sys/wait.h>
#include <unistd.h>

namespace {

struct ChildContext {
    SharedSemaphores* sync;
    bool self_test;
};

int child_main(void* raw_context) {
    auto& context = *static_cast<ChildContext*>(raw_context);

    try {
        if (!context.sync->wait_for_client()) {
            return 11;
        }

        // Open the transport in the child after fork().
        auto connection = make_connection(getppid(), false);

        // Both processes must use the same shared semaphores.
        chat::ChatEndpoint endpoint(
            *connection,
            chat::Participant::Client,
            chat::ChatEndpoint::Clock::now(),
            context.sync
        );

        context.sync->notify_host();

        if (!endpoint.send(
                chat::MessageKind::Broadcast,
                "Client connected"
            )) {
            return 12;
        }

        if (context.self_test) {
            for (const std::string_view expected :
                 {"ping", "ping2"}) {

                const auto request = endpoint.receive();

                if (!request || request->text != expected) {
                    return 13;
                }
            }

            return endpoint.send(
                chat::MessageKind::Private,
                "pong"
            ) ? 0 : 14;
        }

        std::atomic<bool> running{true};
        std::mutex output_mutex;

        // Receive incoming messages independently of stdin.
        std::jthread receiver(
            [&](std::stop_token stop) {
                while (!stop.stop_requested() &&
                       running.load()) {
                    try {
                        const auto message = endpoint.receive();

                        if (!message) {
                            continue;
                        }

                        std::lock_guard lock(output_mutex);

                        const char* kind =
                            message->kind ==
                                chat::MessageKind::Private
                                ? "private"
                                : "broadcast";

                        std::cout
                            << "\n[Host][" << kind << "] "
                            << message->text
                            << "\n> " << std::flush;

                    } catch (const std::exception& error) {
                        std::lock_guard lock(output_mutex);

                        std::cerr
                            << "\nConnection error: "
                            << error.what()
                            << '\n';

                        running.store(false);
                        break;
                    }
                }
            }
        );

        {
            std::lock_guard lock(output_mutex);

            std::cout
                << "\nClient connected.\n"
                << "Write a message to send a broadcast.\n"
                << "Use /private MESSAGE for a private message.\n"
                << "Use /quit to exit.\n"
                << "> " << std::flush;
        }

        std::string line;

        while (running.load()) {
            pollfd input_descriptor{
                STDIN_FILENO,
                POLLIN,
                0
            };

            const int ready = ::poll(
                &input_descriptor,
                1,
                200
            );

            if (ready == -1) {
                if (errno == EINTR) {
                    continue;
                }

                throw std::system_error(
                    errno,
                    std::generic_category(),
                    "poll(stdin)"
                );
            }

            if (ready == 0) {
                continue;
            }

            if ((input_descriptor.revents &
                 (POLLERR | POLLNVAL)) != 0) {
                break;
            }

            if ((input_descriptor.revents & POLLIN) == 0) {
                if ((input_descriptor.revents & POLLHUP) != 0) {
                    break;
                }

                continue;
            }

            if (!std::getline(std::cin, line)) {
                break;
            }

            if (line == "/quit") {
                break;
            }

            auto kind = chat::MessageKind::Broadcast;

            if (line.starts_with("/private ")) {
                kind = chat::MessageKind::Private;
                line.erase(0, 9);
            }

            if (line.empty()) {
                continue;
            }

            const bool sent = endpoint.send(
                kind,
                std::move(line)
            );

            std::lock_guard lock(output_mutex);

            if (!sent) {
                std::cout << "Message was not sent.\n";
            }

            std::cout << "> " << std::flush;
        }

        running.store(false);
        receiver.request_stop();

        return 0;

    } catch (const std::exception& error) {
        std::cerr
            << "Client error: "
            << error.what()
            << '\n';

        return 20;
    }
}

int run_host(int argc, char* argv[], bool self_test) {
    // Shared semaphores must be initialized before fork().
    SharedSemaphores sync;

    ChildContext context{&sync, self_test};

    // Fork before QApplication and worker threads.
    auto child = ChildProcess::spawn(
        child_main,
        &context
    );

    // Create host transport resources after fork().
    auto connection = make_connection(getpid(), true);

    sync.notify_client();

    if (!sync.wait_for_host()) {
        throw std::runtime_error(
            "client connection initialization timed out"
        );
    }

    chat::ChatEndpoint endpoint(
        *connection,
        chat::Participant::Host,
        chat::ChatEndpoint::Clock::now(),
        &sync
    );

    // Receive the initial client handshake.
    const auto greeting = endpoint.receive();

    if (!greeting ||
        greeting->sender != chat::Participant::Client ||
        greeting->text != "Client connected") {
        throw std::runtime_error(
            "client handshake message was not received"
        );
    }

    if (self_test) {
        // The second send requires an acknowledgment
        // from the client for the first message.
        if (!endpoint.send(
                chat::MessageKind::Broadcast,
                "ping"
            ) ||
            !endpoint.send(
                chat::MessageKind::Broadcast,
                "ping2"
            )) {
            throw std::runtime_error(
                "self-test messages failed"
            );
        }

        const auto reply = endpoint.receive();
        const int status = child.wait();

        if (!reply ||
            reply->text != "pong" ||
            !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0) {
            throw std::runtime_error(
                "IPC end-to-end test failed"
            );
        }

        std::cout
            << "IPC self-test passed: "
            << transport_name()
            << '\n';

        return 0;
    }

    // Only the parent process creates the Qt application.
    QApplication app(argc, argv);

    std::atomic<bool> connected{true};
    std::mutex error_mutex;
    std::string error_text;

    // Read IPC messages without blocking the Qt event loop.
    std::jthread receiver(
        [&](std::stop_token stop) {
            while (!stop.stop_requested() &&
                   connected.load()) {
                try {
                    (void)endpoint.receive();

                } catch (const std::exception& error) {
                    {
                        std::lock_guard lock(error_mutex);
                        error_text = error.what();
                    }

                    connected.store(false);
                    break;
                }
            }
        }
    );

    ChatWindow window(
        endpoint,
        child,
        connected,
        error_mutex,
        error_text
    );

    window.show();

    const int result = app.exec();

    connected.store(false);

    if (child.pid() > 0) {
        child.terminate(SIGKILL);
    }

    receiver.request_stop();
    receiver.join();

    return result;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::string_view mode =
        argc == 2
            ? std::string_view(argv[1])
            : std::string_view{};

    if (mode == "--transport") {
        std::cout
            << "Transport: "
            << transport_name()
            << '\n';

        return 0;
    }

    try {
        return run_host(
            argc,
            argv,
            mode == "--self-test"
        );

    } catch (const std::exception& error) {
        std::cerr
            << "Host error: "
            << error.what()
            << '\n';

        return 1;
    }
}
