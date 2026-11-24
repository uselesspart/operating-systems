#pragma once

#include "game/game_types.hpp"

#include <map>
#include <set>
#include <vector>

namespace lab2::game {

class GameEngine {
public:
    void addClient(ClientId clientId);
    bool removeClient(ClientId clientId);

    [[nodiscard]] bool hasClient(ClientId clientId) const;
    [[nodiscard]] std::size_t clientCount() const noexcept;
    [[nodiscard]] KidState clientState(ClientId clientId) const;

    [[nodiscard]] bool isRoundActive() const noexcept;
    [[nodiscard]] bool isGameOver() const noexcept;

    [[nodiscard]] std::size_t consecutiveAllDeadRounds() const noexcept;

    std::vector<ClientId> beginRound();

    RoundResult finishRound(int wolfNumber, const ClientMoves& moves);

private:
    std::map<ClientId, KidState> clients_;
    std::set<ClientId> roundParticipants_;

    std::size_t initialKidCount_{};
    std::size_t roundNumber_{};
    std::size_t consecutiveAllDeadRounds_{};

    bool roundActive_{};
    bool gameOver_{};
};

} // namespace lab2::game
