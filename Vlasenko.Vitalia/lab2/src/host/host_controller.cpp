#include "host/host_controller.hpp"

#include "game/game_engine.hpp"
#include "handshake/handshake_server.hpp"
#include "handshake/handshake_signals.hpp"
#include "host/client_session.hpp"
#include "ipc/connection_factory.hpp"

#include <cerrno>
#include <climits>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lab2::host {
namespace {
constexpr std::chrono::seconds kWolfInputTimeout{3};
constexpr std::chrono::milliseconds kCoordinatorInterval{20};

std::string clientLabel(game::ClientId id, pid_t pid) {
    return "Client " + std::to_string(id) + " (PID " + std::to_string(pid) + ")";
}

const char* outcomeName(game::KidOutcome outcome) {
    switch (outcome) {
    case game::KidOutcome::Hidden:
        return "Hidden";
    case game::KidOutcome::Caught:
        return "Caught";
    case game::KidOutcome::Revived:
        return "Revived";
    case game::KidOutcome::RemainedDead:
        return "Remained dead";
    }
    return "Unknown";
}

std::string describeRound(const game::RoundResult& result) {
    std::ostringstream text;
    text << "Round " << result.roundNumber << ": wolf = " << result.wolfNumber
         << ", hidden = " << result.hiddenCount << ", caught = " << result.caughtCount
         << ", revived = " << result.revivedCount << ", dead = " << result.deadCount;
    for (const auto& kid : result.kids) {
        text << "\n  Client " << kid.clientId << ": " << kid.number << " - "
             << outcomeName(kid.outcome);
    }
    return text.str();
}
} // namespace

class HostController::Coordinator {
public:
    explicit Coordinator(HostController& host) : host_(host) {
    }
    ~Coordinator() {
        for (auto& entry : sessions_) {
            entry.second->requestStop();
        }
        for (auto& entry : sessions_) {
            entry.second->join();
        }
    }

    void run() {
        publishWaiting();
        while (!host_.stopping_.load() && !game_.isGameOver()) {
            updateSessions();
            if (host_.stopping_.load()) {
                break;
            }
            advanceRound();
            if (!game_.isGameOver() && !host_.stopping_.load()) {
                acceptNext();
            }
        }
        host_.closeInput();
        if (game_.isGameOver()) {
            // RoundResult уже находится в очереди каждого участника, GameOver идёт строго после
            // него.
            for (auto& entry : sessions_) {
                entry.second->finishGame(roundNumber_);
            }
        } else {
            for (auto& entry : sessions_) {
                entry.second->requestStop();
            }
        }
        for (auto& entry : sessions_) {
            entry.second->join();
        }
        updateSessions();
        if (game_.isGameOver()) {
            host_.publish({HostEventType::GameFinished, "Game over", roundNumber_});
        }
    }

private:
    void publishWaiting() {
        if (!waitingForClients_) {
            host_.publish({HostEventType::Status, "Waiting for clients"});
            waitingForClients_ = true;
        }
    }

    void acceptNext() {
        std::optional<handshake::HandshakeResult> result;
        try {
            result = handshake_.handleNext(
                [this](pid_t pid) {
                    if (nextId_ > static_cast<std::uint32_t>(INT_MAX)) {
                        throw std::overflow_error("Client IDs exhausted");
                    }
                    const auto id = nextId_++;
                    auto connection = ipc::prepareHostConnection(id, pid);
                    auto session = std::make_unique<ClientSession>(id, pid, std::move(connection));
                    sessions_.emplace(id, std::move(session));
                    return id;
                },
                kCoordinatorInterval);
        } catch (const std::exception& error) {
            host_.publish({HostEventType::Error, std::string("Handshake failed: ") + error.what()});
            return;
        }
        if (!result) {
            return;
        }
        const auto id = result->clientId;
        if (result->replyError && result->replyError.value() != EAGAIN) {
            host_.publish(
                {HostEventType::Error, "Handshake reply failed: " + result->replyError.message()});
            const auto session = sessions_.find(id);
            if (session != sessions_.end()) {
                if (result->newSession) {
                    sessions_.erase(session);
                    handshake_.forget(result->clientPid);
                } else {
                    session->second->requestStop();
                }
            }
            return;
        }
        if (result->newSession) {
            try {
                sessions_.at(id)->start();
            } catch (const std::exception& error) {
                sessions_.erase(id);
                handshake_.forget(result->clientPid);
                host_.publish({HostEventType::Error,
                               std::string("Cannot start client thread: ") + error.what()});
            }
        }
    }

    void updateSessions() {
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            const auto id = it->first;
            auto& session = *it->second;
            const auto status = session.status();
            if (status.finished) {
                session.join();
                const bool registered = game_.removeClient(id);
                participants_.erase(id);
                handshake_.forget(session.pid());
                const auto label = clientLabel(id, session.pid());
                if (!status.error.empty()) {
                    host_.publish({HostEventType::Error, label + ": " + status.error});
                }
                if (registered) {
                    host_.publish(
                        {HostEventType::ClientLeft, label + " disconnected", 0, id, session.pid()});
                }
                it = sessions_.erase(it);
                continue;
            }
            if (status.ready && !game_.hasClient(id) && !game_.isGameOver() &&
                !host_.stopping_.load()) {
                game_.addClient(id);
                host_.publish({HostEventType::ClientJoined,
                               clientLabel(id, session.pid()) + " connected", 0, id,
                               session.pid()});
            }
            ++it;
        }
    }

    void advanceRound() {
        if (!game_.isRoundActive()) {
            if (game_.clientCount() == 0) {
                publishWaiting();
                return;
            }
            waitingForClients_ = false;
            const auto ids = game_.beginRound();
            participants_ = std::set<game::ClientId>(ids.begin(), ids.end());
            ++roundNumber_;
            wolfNumber_.reset();
            host_.openInput(roundNumber_);
            host_.publish({HostEventType::RoundStarted,
                           "Round " + std::to_string(roundNumber_) +
                               " started; enter wolf number within 3 seconds",
                           roundNumber_});
            for (const auto id : participants_) {
                sessions_.at(id)->startRound(roundNumber_, game_.clientState(id));
            }
        }

        if (participants_.empty()) {
            host_.closeInput();
            const auto result = game_.finishRound(game::kMinNumber, {});
            host_.publish({HostEventType::RoundFinished,
                           "Round cancelled: no remaining participants", roundNumber_, 0, 0,
                           result});
            if (game_.clientCount() == 0) {
                publishWaiting();
            }
            return;
        }
        if (!wolfNumber_) {
            wolfNumber_ = host_.takeWolfNumber();
        }
        if (!wolfNumber_) {
            return;
        }
        game::ClientMoves moves;
        for (const auto id : participants_) {
            const auto status = sessions_.at(id)->status();
            if (!status.move || status.finished) {
                return;
            }
            moves.emplace(id, *status.move);
        }
        const auto result = game_.finishRound(*wolfNumber_, moves);
        for (const auto& kid : result.kids) {
            sessions_.at(kid.clientId)->sendResult(result.roundNumber, kid.currentState);
        }
        host_.publish({HostEventType::RoundFinished, describeRound(result), result.roundNumber, 0,
                       0, result});
    }

    HostController& host_;
    handshake::HandshakeServer handshake_;
    game::GameEngine game_;
    std::map<game::ClientId, std::unique_ptr<ClientSession>> sessions_;
    std::set<game::ClientId> participants_;
    std::uint32_t nextId_{1};
    std::uint64_t roundNumber_{};
    std::optional<int> wolfNumber_;
    bool waitingForClients_{};
};

HostController::~HostController() {
    requestStop();
    join();
}

void HostController::start() {
    if (started_) {
        throw std::logic_error("Host already started");
    }
    handshake::requireHandshakeSignalsBlocked();
    thread_ = std::thread(&HostController::run, this);
    started_ = true;
}

void HostController::requestStop() noexcept {
    stopping_.store(true);
}

void HostController::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HostController::run() {
    try {
        Coordinator coordinator(*this);
        coordinator.run();
    } catch (const std::exception& error) {
        publish({HostEventType::Error, std::string("Host failed: ") + error.what()});
    }
    closeInput();
    publish({HostEventType::Stopped, "Host stopped"});
}

void HostController::publish(HostEvent event) {
    if (event.type == HostEventType::Error) {
        logger_.error(event.text);
    } else {
        logger_.info(event.text);
    }
    const std::lock_guard<std::mutex> lock(eventsMutex_);
    events_.push_back(std::move(event));
}

std::vector<HostEvent> HostController::drainEvents() {
    const std::lock_guard<std::mutex> lock(eventsMutex_);
    std::vector<HostEvent> result;
    result.swap(events_);
    return result;
}

void HostController::openInput(std::uint64_t roundNumber) {
    const std::lock_guard<std::mutex> lock(inputMutex_);
    inputRound_ = roundNumber;
    inputDeadline_ = std::chrono::steady_clock::now() + kWolfInputTimeout;
    submittedNumber_.reset();
}

void HostController::closeInput() {
    const std::lock_guard<std::mutex> lock(inputMutex_);
    inputRound_ = 0;
    submittedNumber_.reset();
}

bool HostController::submitWolfNumber(std::uint64_t roundNumber, int number) {
    const std::lock_guard<std::mutex> lock(inputMutex_);
    if (stopping_.load() || roundNumber == 0 || roundNumber != inputRound_ || submittedNumber_ ||
        std::chrono::steady_clock::now() >= inputDeadline_ || number < game::kMinNumber ||
        number > game::kMaxWolfNumber) {
        return false;
    }
    submittedNumber_ = number;
    return true;
}

std::chrono::milliseconds HostController::inputTimeRemaining(std::uint64_t roundNumber) {
    const std::lock_guard<std::mutex> lock(inputMutex_);
    if (stopping_.load() || roundNumber == 0 || roundNumber != inputRound_ || submittedNumber_) {
        return std::chrono::milliseconds::zero();
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= inputDeadline_) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::ceil<std::chrono::milliseconds>(inputDeadline_ - now);
}

std::optional<int> HostController::takeWolfNumber() {
    const std::lock_guard<std::mutex> lock(inputMutex_);
    if (inputRound_ == 0) {
        return std::nullopt;
    }
    if (!submittedNumber_ && std::chrono::steady_clock::now() < inputDeadline_) {
        return std::nullopt;
    }
    // Генератор принадлежит потоку координатора, а не GUI или потокам сессий.
    int number;
    if (submittedNumber_) {
        number = *submittedNumber_;
    } else {
        static thread_local std::mt19937 random(std::random_device{}());
        number = std::uniform_int_distribution<int>(game::kMinNumber, game::kMaxWolfNumber)(random);
    }
    inputRound_ = 0;
    submittedNumber_.reset();
    return number;
}

} // namespace lab2::host
