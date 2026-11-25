#include "connection.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using Message = std::array<unsigned char, wolf::Connection::capacity>;

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void runChild(wolf::Connection& connection, bool timeout)
{
    connection.open(wolf::Side::child);
    if (timeout) {
        return;
    }

    Message message{};
    for (unsigned char expected : {17, 53}) {
        connection.read(message.data(), message.size());
        Message original{};
        original.fill(expected);
        check(message == original, "Козлёнок получил повреждённые данные");
    }
    connection.write(message.data(), message.size());
}

void checkTimeout(wolf::Connection& connection)
{
    const auto start = std::chrono::steady_clock::now();
    Message message{};
    try {
        connection.read(message.data(), message.size());
    } catch (const std::system_error& error) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        check(error.code() == std::errc::timed_out, "Получена ошибка вместо истечения тайм-аута");
        check(elapsed >= std::chrono::seconds(5) && elapsed < std::chrono::seconds(8),
              "Ожидание должно завершиться примерно через 5 секунд");
        return;
    }
    throw std::runtime_error("Чтение завершилось, хотя клиент ничего не отправил");
}

void checkExchange(wolf::Connection& connection)
{
    Message first{};
    first.fill(17);
    Message second{};
    second.fill(53);
    connection.write(first.data(), first.size());
    connection.write(second.data(), second.size());

    Message reply{};
    connection.read(reply.data(), reply.size());
    check(reply == second, "Хост получил повреждённые данные");
}

int waitForChild(pid_t child)
{
    int status = 0;
    pid_t result;
    do {
        result = waitpid(child, &status, 0);
    } while (result == -1 && errno == EINTR);
    check(result == child, "Не удалось дождаться дочернего процесса");
    return status;
}

void testConnection(bool timeout)
{
    auto connection = wolf::makeConnection();
    const pid_t child = fork();
    check(child != -1, "Не удалось создать дочерний процесс");

    if (child == 0) {
        try {
            runChild(*connection, timeout);
            _exit(EXIT_SUCCESS);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            _exit(EXIT_FAILURE);
        }
    }

    try {
        connection->open(wolf::Side::host);
        if (timeout) {
            checkTimeout(*connection);
        } else {
            checkExchange(*connection);
        }
    } catch (...) {
        kill(child, SIGKILL);
        waitForChild(child);
        throw;
    }

    const int status = waitForChild(child);
    check(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS,
          "Дочерний процесс завершился с ошибкой");
}

}

int main(int argc, char* argv[])
{
    try {
        std::signal(SIGPIPE, SIG_IGN);
        testConnection(argc == 2 && std::string_view(argv[1]) == "timeout");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
