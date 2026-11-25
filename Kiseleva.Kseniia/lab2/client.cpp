
#include "connection.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <poll.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace {

std::uint64_t Timestamp() {
    using namespace std::chrono;

    return static_cast<std::uint64_t>(
        duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count()
    );
}

void CopyText(
    char* destination,
    std::size_t size,
    const std::string& source
) {
    std::snprintf(destination, size, "%s", source.c_str());
}

void ReceiveMessages(
    Connection* connection,
    std::atomic<bool>& running,
    std::atomic<bool>& user_quit
) {
    while (running.load()) {
        ChatMessage message{};

        if (!connection->Read(message, 5000)) {
            if (!user_quit.load()) {
                std::cerr << "\nСоединение с Host потеряно.\n";
            }

            running.store(false);
            break;
        }

        if (message.type == MessageType::Heartbeat) {
            continue;
        }

        if (message.type == MessageType::Exit) {
            std::cout << "\nHost завершил соединение.\n";
            running.store(false);
            break;
        }

        std::cout << "\n[" << message.sender << "] "
                  << message.text
                  << "\n> " << std::flush;
    }
}

} // namespace

int RunClient(pid_t host_pid, SharedSync* sync, int fd) {
    auto connection = CreateConnection(
        ProcessRole::Client,
        host_pid,
        sync,
        fd
    );

    if (!connection) {
        return 1;
    }

    std::atomic<bool> running{true};
    std::atomic<bool> user_quit{false};

    std::thread receiver(
        ReceiveMessages,
        connection.get(),
        std::ref(running),
        std::ref(user_quit)
    );

    std::cout
        << "\nПодключение к локальному чату.\n"
        << "Обычный текст — публичное сообщение.\n"
        << "/w host текст — личное сообщение Host.\n"
        << "/quit — выход.\n\n> "
        << std::flush;

    while (running.load()) {
        pollfd pfd{};
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;

        const int result = poll(&pfd, 1, 500);

        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }

            break;
        }

        if (result == 0) {
            continue;
        }

        std::string line;

        if (!std::getline(std::cin, line)) {
            break;
        }

        if (line == "/quit") {
            user_quit.store(true);
            break;
        }

        if (line.empty()) {
            std::cout << "> " << std::flush;
            continue;
        }

        ChatMessage message{};
        message.timestamp_ms = Timestamp();

        CopyText(
            message.sender,
            sizeof(message.sender),
            "client"
        );

        if (line.rfind("/w host ", 0) == 0) {
            message.type = MessageType::Private;

            CopyText(
                message.recipient,
                sizeof(message.recipient),
                "host"
            );

            line = line.substr(8);
        } else {
            message.type = MessageType::Public;

            CopyText(
                message.recipient,
                sizeof(message.recipient),
                "all"
            );
        }

        CopyText(
            message.text,
            sizeof(message.text),
            line
        );

        if (!connection->Write(message, 5000)) {
            std::cerr << "Не удалось отправить сообщение.\n";
            break;
        }

        std::cout << "> " << std::flush;
    }

    // Сначала отправляем Host уведомление о выходе.
    ChatMessage exit_message{};
    exit_message.type = MessageType::Exit;
    exit_message.timestamp_ms = Timestamp();

    CopyText(
        exit_message.sender,
        sizeof(exit_message.sender),
        "client"
    );

    connection->Write(exit_message, 1000);

    running.store(false);

    if (receiver.joinable()) {
        receiver.join();
    }

    return 0;
}
