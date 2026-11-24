#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace lab2::game {

using ClientId = std::uint32_t;

inline constexpr int kMinNumber = 1;
inline constexpr int kMaxWolfNumber = 100;
inline constexpr int kMaxAliveNumber = 100;
inline constexpr int kMaxDeadNumber = 50;

inline constexpr int kHideThreshold = 70;
inline constexpr int kReviveThreshold = 20;

inline constexpr std::size_t kGameOverThreshold = 2;

enum class KidState { Alive, Dead };

enum class KidOutcome { Hidden, Caught, Revived, RemainedDead };

using ClientMoves = std::map<ClientId, int>;

struct KidRoundResult {
    ClientId clientId{};
    int number{};
    KidState previousState{};
    KidState currentState{};
    KidOutcome outcome{};
};

struct RoundResult {
    std::size_t roundNumber{};
    int wolfNumber{};
    std::size_t initialKidCount{};

    std::vector<KidRoundResult> kids;

    std::size_t hiddenCount{};
    std::size_t caughtCount{};
    std::size_t revivedCount{};
    std::size_t deadCount{};
    std::size_t aliveCount{};

    std::size_t consecutiveAllDeadRounds{};

    bool completed{};
    bool gameOver{};
};

} // namespace lab2::game
