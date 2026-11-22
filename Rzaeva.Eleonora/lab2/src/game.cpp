#include "game.hpp"

#include "connection.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>

namespace wolf {
namespace {

enum class Command { roll, alive, dead, finish };

int rollNumber(int maximum)
{
    thread_local std::mt19937 generator(std::random_device{}());
    return std::uniform_int_distribution<int>(1, maximum)(generator);
}

void sendCommand(Connection& connection, Command command)
{
    connection.write(&command, sizeof(command));
}

Command readCommand(Connection& connection)
{
    Command command{};
    connection.read(&command, sizeof(command));
    return command;
}

int runChild(Connection& connection)
{
    connection.open(Side::child);
    bool alive = true;

    while (true) {
        if (readCommand(connection) != Command::roll) {
            throw std::runtime_error("Козлёнок ожидал начало раунда");
        }
        const int number = rollNumber(alive ? 100 : 50);
        connection.write(&number, sizeof(number));

        switch (readCommand(connection)) {
        case Command::alive:
            alive = true;
            break;
        case Command::dead:
            alive = false;
            break;
        case Command::finish:
            sendCommand(connection, Command::finish);
            return EXIT_SUCCESS;
        default:
            throw std::runtime_error("Козлёнок получил неизвестный результат раунда");
        }
    }
}

}

Round Game::play(int wolfNumber, int kidNumber)
{
    if (roundsAllDead_ == 2) {
        throw std::logic_error("Игра уже закончена");
    }
    if (wolfNumber < 1 || wolfNumber > 100 ||
        kidNumber < 1 || kidNumber > (alive_ ? 100 : 50)) {
        throw std::invalid_argument("Число вне допустимого диапазона");
    }

    const bool wasAlive = alive_;
    // В варианте «один к одному» пороги 70/n и 20/n равны 70 и 20.
    alive_ = std::abs(wolfNumber - kidNumber) <= (wasAlive ? 70 : 20);
    roundsAllDead_ = alive_ ? 0 : roundsAllDead_ + 1;
    return {wolfNumber, kidNumber, wasAlive, alive_, roundsAllDead_ == 2};
}

struct GameSession::Process {
    ~Process()
    {
        if (pid <= 0) {
            return;
        }

        pid_t result;
        do {
            result = waitpid(pid, nullptr, WNOHANG);
        } while (result == -1 && errno == EINTR);
        if (result == 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {
            }
        }
    }

    std::unique_ptr<Connection> connection = makeConnection();
    pid_t pid = -1;
};

GameSession::GameSession() : process_(std::make_unique<Process>())
{
    // При закрытии канала запись должна вернуть EPIPE вместо завершения хоста.
    if (std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        throw std::system_error(errno, std::generic_category(), "Не удалось отключить SIGPIPE");
    }

    process_->pid = fork();
    if (process_->pid == -1) {
        throw std::system_error(errno, std::generic_category(), "Не удалось запустить козлёнка");
    }
    if (process_->pid == 0) {
        try {
            // Общие семафоры уничтожает хост после завершения дочернего процесса.
            _exit(runChild(*process_->connection));
        } catch (const std::exception& error) {
            std::cerr << "Козлёнок: " << error.what() << '\n';
            _exit(EXIT_FAILURE);
        }
    }
    process_->connection->open(Side::host);
}

GameSession::~GameSession() = default;

Round GameSession::play(int wolfNumber)
{
    auto& connection = *process_->connection;
    sendCommand(connection, Command::roll);
    int kidNumber = 0;
    connection.read(&kidNumber, sizeof(kidNumber));
    const Round round = game_.play(wolfNumber, kidNumber);
    if (round.finished) {
        sendCommand(connection, Command::finish);
        if (readCommand(connection) != Command::finish) {
            throw std::runtime_error("Козлёнок не подтвердил завершение игры");
        }
    } else {
        sendCommand(connection, round.alive ? Command::alive : Command::dead);
    }
    return round;
}

int rollWolfNumber()
{
    return rollNumber(100);
}

}
