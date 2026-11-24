#include "game/game_engine.hpp"

#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace lab2::game {
namespace {

bool isValidWolfNumber(int number) noexcept {
    return number >= kMinNumber && number <= kMaxWolfNumber;
}

bool isValidKidNumber(int number, KidState state) noexcept {
    const int maxNumber = state == KidState::Alive ? kMaxAliveNumber : kMaxDeadNumber;

    return number >= kMinNumber && number <= maxNumber;
}

KidRoundResult evaluateKid(ClientId clientId, KidState state, int wolfNumber, int kidNumber,
                           std::size_t kidCount) {
    const auto difference = static_cast<std::size_t>(std::abs(wolfNumber - kidNumber));

    KidRoundResult result{clientId, kidNumber, state, state, KidOutcome::RemainedDead};

    // Для целочисленной разности округление порога вниз
    // сохраняет результат сравнения с точным значением 70/n или 20/n.
    if (state == KidState::Alive) {
        if (difference <= kHideThreshold / kidCount) {
            result.outcome = KidOutcome::Hidden;
        } else {
            result.currentState = KidState::Dead;
            result.outcome = KidOutcome::Caught;
        }
    } else {
        if (difference <= kReviveThreshold / kidCount) {
            result.currentState = KidState::Alive;
            result.outcome = KidOutcome::Revived;
        }
    }

    return result;
}

void updateStatistics(RoundResult& round, const KidRoundResult& kid) {
    switch (kid.outcome) {
    case KidOutcome::Hidden:
        ++round.hiddenCount;
        break;

    case KidOutcome::Caught:
        ++round.caughtCount;
        break;

    case KidOutcome::Revived:
        ++round.revivedCount;
        break;

    case KidOutcome::RemainedDead:
        break;
    }

    if (kid.currentState == KidState::Alive) {
        ++round.aliveCount;
    } else {
        ++round.deadCount;
    }
}

} // namespace

void GameEngine::addClient(ClientId clientId) {
    if (gameOver_) {
        throw std::logic_error("Game is already over");
    }

    if (clientId == 0) {
        throw std::invalid_argument("Invalid client ID");
    }

    const auto inserted = clients_.emplace(clientId, KidState::Alive);

    if (!inserted.second) {
        throw std::invalid_argument("Client already exists");
    }
}

bool GameEngine::removeClient(ClientId clientId) {
    if (clients_.erase(clientId) == 0) {
        return false;
    }

    // Состав раунда может уменьшиться, но знаменатель
    // в игровых формулах остаётся зафиксированным.
    roundParticipants_.erase(clientId);

    if (clients_.empty() && !gameOver_) {
        consecutiveAllDeadRounds_ = 0;
    }

    return true;
}

bool GameEngine::hasClient(ClientId clientId) const {
    return clients_.find(clientId) != clients_.end();
}

std::size_t GameEngine::clientCount() const noexcept {
    return clients_.size();
}

KidState GameEngine::clientState(ClientId clientId) const {
    return clients_.at(clientId);
}

bool GameEngine::isRoundActive() const noexcept {
    return roundActive_;
}

bool GameEngine::isGameOver() const noexcept {
    return gameOver_;
}

std::size_t GameEngine::consecutiveAllDeadRounds() const noexcept {
    return consecutiveAllDeadRounds_;
}

std::vector<ClientId> GameEngine::beginRound() {
    if (gameOver_) {
        throw std::logic_error("Game is already over");
    }

    if (roundActive_) {
        throw std::logic_error("Round is already active");
    }

    if (clients_.empty()) {
        throw std::logic_error("Cannot start round without clients");
    }

    std::set<ClientId> participants;
    std::vector<ClientId> clientIds;

    clientIds.reserve(clients_.size());

    for (const auto& [clientId, state] : clients_) {
        participants.insert(clientId);
        clientIds.push_back(clientId);
    }

    roundParticipants_ = std::move(participants);
    initialKidCount_ = roundParticipants_.size();

    ++roundNumber_;
    roundActive_ = true;

    return clientIds;
}

RoundResult GameEngine::finishRound(int wolfNumber, const ClientMoves& moves) {
    if (!roundActive_) {
        throw std::logic_error("No active round");
    }

    if (!isValidWolfNumber(wolfNumber)) {
        throw std::invalid_argument("Invalid wolf number");
    }

    if (moves.size() != roundParticipants_.size()) {
        throw std::invalid_argument("Unexpected number of client moves");
    }

    // Проверяем все входные данные до изменения состояний:
    // некорректный ход не должен частично завершать раунд.
    for (const auto& [clientId, number] : moves) {
        if (roundParticipants_.count(clientId) == 0) {
            throw std::invalid_argument("Unexpected client move");
        }

        if (!isValidKidNumber(number, clients_.at(clientId))) {
            throw std::invalid_argument("Invalid client number");
        }
    }

    RoundResult result;
    result.roundNumber = roundNumber_;
    result.wolfNumber = wolfNumber;
    result.initialKidCount = initialKidCount_;

    result.kids.reserve(roundParticipants_.size());

    for (const ClientId clientId : roundParticipants_) {
        const auto kidResult = evaluateKid(clientId, clients_.at(clientId), wolfNumber,
                                           moves.at(clientId), initialKidCount_);

        result.kids.push_back(kidResult);
        updateStatistics(result, kidResult);
    }

    for (const auto& kid : result.kids) {
        clients_.at(kid.clientId) = kid.currentState;
    }

    // Пустой раунд не считается раундом, в котором все погибли.
    result.completed = !result.kids.empty();

    if (result.completed) {
        if (result.aliveCount == 0) {
            ++consecutiveAllDeadRounds_;
        } else {
            consecutiveAllDeadRounds_ = 0;
        }
    }

    gameOver_ = consecutiveAllDeadRounds_ >= kGameOverThreshold;

    result.consecutiveAllDeadRounds = consecutiveAllDeadRounds_;
    result.gameOver = gameOver_;

    roundParticipants_.clear();
    initialKidCount_ = 0;
    roundActive_ = false;

    return result;
}

} // namespace lab2::game
