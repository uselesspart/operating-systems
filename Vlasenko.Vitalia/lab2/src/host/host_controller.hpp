#pragma once

#include "common/logger.hpp"
#include "game/game_types.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace lab2::host {

enum class HostEventType {
    Status,
    ClientJoined,
    ClientLeft,
    RoundStarted,
    RoundFinished,
    GameFinished,
    Stopped,
    Error
};

struct HostEvent {
    HostEventType type{HostEventType::Status};
    std::string text;
    std::uint64_t roundNumber{};
    game::ClientId clientId{};
    pid_t clientPid{};
    std::optional<game::RoundResult> result{};
};

class HostController {
public:
    HostController() = default;
    ~HostController();
    HostController(const HostController&) = delete;
    HostController& operator=(const HostController&) = delete;

    // Вызывается после блокировки сигналов в главном потоке.
    void start();
    void requestStop() noexcept;
    void join();

    bool submitWolfNumber(std::uint64_t roundNumber, int number);
    [[nodiscard]] std::chrono::milliseconds inputTimeRemaining(std::uint64_t roundNumber);
    [[nodiscard]] std::vector<HostEvent> drainEvents();

private:
    class Coordinator;
    void run();
    void publish(HostEvent event);
    void openInput(std::uint64_t roundNumber);
    void closeInput();
    std::optional<int> takeWolfNumber();

    std::atomic<bool> stopping_{false};
    bool started_{};
    std::thread thread_;
    common::Logger logger_;
    std::mutex eventsMutex_;
    std::vector<HostEvent> events_;

    std::mutex inputMutex_;
    std::uint64_t inputRound_{};
    std::chrono::steady_clock::time_point inputDeadline_{};
    std::optional<int> submittedNumber_;
};

} // namespace lab2::host
