
#include "connection.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAIL: %s\n", message); \
            return 1; \
        } \
    } while (false)

namespace {

ChatMessage MakeMessage(const char* text) {
    ChatMessage message{};
    message.type = MessageType::Public;
    message.timestamp_ms = 123456;
    std::snprintf(message.sender, sizeof(message.sender), "test");
    std::snprintf(message.recipient, sizeof(message.recipient), "host");
    std::snprintf(message.text, sizeof(message.text), "%s", text);
    return message;
}

bool SameText(const ChatMessage& message, const char* text) {
    return std::strcmp(message.text, text) == 0;
}

} // namespace

int main() {
    int host_fd = -1;
    int client_fd = -1;

    CHECK(PrepareConnection(host_fd, client_fd) == 0,
          "PrepareConnection failed");

    auto* sync = static_cast<SharedSync*>(
        mmap(nullptr, sizeof(SharedSync),
             PROT_READ | PROT_WRITE,
             MAP_SHARED | MAP_ANONYMOUS, -1, 0)
    );

    CHECK(sync != MAP_FAILED, "mmap failed");

    CHECK(sem_init(&sync->host_to_client, 1, 0) == 0,
          "host_to_client sem_init failed");

    CHECK(sem_init(&sync->client_to_host, 1, 0) == 0,
          "client_to_host sem_init failed");

    const pid_t host_pid = getpid();
    const pid_t child = fork();

    CHECK(child >= 0, "fork failed");

    if (child == 0) {
        if (host_fd >= 0) {
            close(host_fd);
        }

        auto client = CreateConnection(
            ProcessRole::Client, host_pid, sync, client_fd
        );

        if (!client) {
            _exit(10);
        }

        // Даём хосту время создать соединение.
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        ChatMessage outgoing = MakeMessage("client-to-host");

        if (!client->Write(outgoing, 2000)) {
            _exit(11);
        }

        // Даём хосту время заполнить очередь.
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        ChatMessage incoming{};

        if (!client->Read(incoming, 2000)) {
            _exit(12);
        }

        if (!SameText(incoming, "host-to-client")) {
            _exit(13);
        }

#ifdef TEST_MQ
        // Проверяем, что клиент может получить все сообщения
        // из заполненной очереди сообщений.
        for (int i = 0; i < 9; ++i) {
            ChatMessage message{};

            if (!client->Read(message, 2000)) {
                _exit(14);
            }

            char expected[32]{};
            std::snprintf(expected, sizeof(expected), "fill-%d", i);

            if (!SameText(message, expected)) {
                _exit(15);
            }
        }
#endif

        _exit(0);
    }

    if (client_fd >= 0) {
        close(client_fd);
    }

    auto host = CreateConnection(
        ProcessRole::Host, host_pid, sync, host_fd
    );

    if (!host) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        return 1;
    }

    // Пока клиент не отправил сообщение, чтение должно завершиться
    // по таймауту.
    ChatMessage incoming{};

    CHECK(!host->Read(incoming, 50),
          "Read should time out when no message is available");

    CHECK(host->Read(incoming, 2000),
          "Host failed to receive client's message");

    CHECK(SameText(incoming, "client-to-host"),
          "Incorrect message received by host");

    ChatMessage reply = MakeMessage("host-to-client");

    CHECK(host->Write(reply, 2000),
          "Host failed to send reply");

#ifdef TEST_MQ
    // Ответ хоста уже занимает одно место в очереди.
    // Добавляем ещё 9 сообщений — очередь из 10 сообщений заполнена.
    for (int i = 0; i < 9; ++i) {
        char text[32]{};
        std::snprintf(text, sizeof(text), "fill-%d", i);

        ChatMessage message = MakeMessage(text);

        CHECK(host->Write(message, 500),
            "Failed to fill message queue");
    }

    ChatMessage extra = MakeMessage("overflow");

    CHECK(!host->Write(extra, 100),
        "Writing to a full message queue should time out");
#endif

    int status = 0;
    const pid_t result = waitpid(child, &status, 0);

    CHECK(result == child, "waitpid failed");
    CHECK(WIFEXITED(status), "Child terminated abnormally");
    CHECK(WEXITSTATUS(status) == 0, "Child test failed");

    host.reset();

    sem_destroy(&sync->host_to_client);
    sem_destroy(&sync->client_to_host);
    munmap(sync, sizeof(SharedSync));

    std::printf("All connection tests passed.\n");
    return 0;
}
