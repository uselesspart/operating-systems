#include "wolf/conn.hpp"
#include "wolf/protocol.hpp"
#include "wolf/shared_state.hpp"
#include "wolf/transport.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[noreturn]] void RunChild(wolf::SharedState* state) {
    try {
        std::unique_ptr<wolf::Conn> connection = wolf::OpenTransport(wolf::Role::Client, state);

        wolf::Message request{};
        const bool request_ok = connection->Read(&request, sizeof(request));
        Require(request_ok, "клиент: " + connection->Error());
        Require(request.type == wolf::RoundStart && request.round == 1 && request.value == 100,
                "клиент получил некорректный запрос");

        const wolf::Message reply{wolf::GoatThrow, 1, 42, 0};
        const bool reply_ok = connection->Write(&reply, sizeof(reply));
        Require(reply_ok, "клиент: " + connection->Error());

        wolf::Message result{};
        const bool result_ok = connection->Read(&result, sizeof(result));
        Require(result_ok, "клиент: " + connection->Error());
        Require(result.type == wolf::RoundResult && result.round == 1 && result.value == 1 &&
                    result.flag == 0,
                "клиент получил некорректный результат");
        _exit(EXIT_SUCCESS);
    } catch (const std::exception& exception) {
        std::cerr << "дочерний процесс: " << exception.what() << '\n';
        _exit(EXIT_FAILURE);
    }
}

void TestRoundTrip() {
    wolf::SharedStateArea area;
    wolf::PrepareTransport(area.Get());

    const pid_t child = fork();
    Require(child >= 0, "fork не удался");
    if (child == 0) {
        RunChild(area.Get());
    }

    try {
        std::unique_ptr<wolf::Conn> connection = wolf::OpenTransport(wolf::Role::Host, area.Get());

        const wolf::Message request{wolf::RoundStart, 1, 100, 0};
        const bool request_ok = connection->Write(&request, sizeof(request));
        Require(request_ok, "хост: " + connection->Error());

        wolf::Message reply{};
        const bool reply_ok = connection->Read(&reply, sizeof(reply));
        Require(reply_ok, "хост: " + connection->Error());
        Require(reply.type == wolf::GoatThrow && reply.round == 1 && reply.value == 42,
                "хост получил некорректный ответ");

        const wolf::Message result{wolf::RoundResult, 1, 1, 0};
        const bool result_ok = connection->Write(&result, sizeof(result));
        Require(result_ok, "хост: " + connection->Error());
    } catch (...) {
        kill(child, SIGTERM);
        int status = 0;
        while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
        }
        throw;
    }

    int status = 0;
    const pid_t waited = waitpid(child, &status, 0);
    Require(waited == child, "waitpid не дождался клиента");
    Require(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS,
            "клиент завершился с ошибкой");
}

void TestReadTimeout() {
    wolf::SharedStateArea area;
    wolf::PrepareTransport(area.Get());
    std::unique_ptr<wolf::Conn> connection = wolf::OpenTransport(wolf::Role::Host, area.Get());

    const auto started = std::chrono::steady_clock::now();
    wolf::Message reply{};
    const bool read_ok = connection->Read(&reply, sizeof(reply));
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started).count();

    Require(!read_ok, "чтение без ответа должно завершиться ошибкой");
    Require(elapsed >= 4, "ожидание должно ограничиваться пятью секундами");
}

void TestCancel() {
    wolf::SharedStateArea area;
    wolf::PrepareTransport(area.Get());
    std::unique_ptr<wolf::Conn> connection = wolf::OpenTransport(wolf::Role::Host, area.Get());

    std::atomic<bool> read_result{true};
    std::thread reader([&connection, &read_result]() {
        wolf::Message message{};
        read_result.store(connection->Read(&message, sizeof(message)));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    connection->Cancel();
    reader.join();
    Require(!read_result.load(), "отменённое чтение не должно завершиться успешно");
}

}

int main() {
    try {
        TestRoundTrip();
        TestReadTimeout();
        TestCancel();
        std::cout << "тесты транспорта пройдены\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "тест не пройден: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
