#pragma once

#include "game/game_types.hpp"
#include "ipc/connection_factory.hpp"
#include "protocol/messages.hpp"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace lab2::host {

struct SessionStatus {
    bool ready{};
    bool finished{};
    std::optional<int> move;
    std::string error;
};

class ClientSession {
public:
    ClientSession(game::ClientId id, pid_t pid, std::unique_ptr<ipc::IHostConnection> connection);
    ~ClientSession();
    ClientSession(const ClientSession&) = delete;
    ClientSession& operator=(const ClientSession&) = delete;

    void start();
    void startRound(std::uint64_t round, game::KidState state);
    void sendResult(std::uint64_t round, game::KidState state);
    void finishGame(std::uint64_t round);
    void requestStop();
    void join();
    [[nodiscard]] SessionStatus status() const;
    [[nodiscard]] pid_t pid() const noexcept;

private:
    struct Command {
        protocol::Message message;
        game::KidState state{game::KidState::Alive};
    };
    void enqueue(Command command);
    void run();
    void exchange();

    game::ClientId id_;
    pid_t pid_;
    std::unique_ptr<ipc::IHostConnection> connection_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Command> commands_;
    SessionStatus status_;
    bool stopping_{};
    std::thread thread_;
};

} // namespace lab2::host
