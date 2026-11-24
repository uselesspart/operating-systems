#include "host/client_session.hpp"

#include "protocol/protocol.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace lab2::host {
namespace {
using protocol::MessageType;
using protocol::WireKidState;
constexpr std::chrono::seconds kWaitingInterval{1};
} // namespace

ClientSession::ClientSession(game::ClientId id, pid_t pid,
                             std::unique_ptr<ipc::IHostConnection> connection)
    : id_(id), pid_(pid), connection_(std::move(connection)) {
    if (id_ == 0 || pid_ <= 0 || !connection_) {
        throw std::invalid_argument("Invalid session configuration");
    }
}

ClientSession::~ClientSession() {
    requestStop();
    join();
}

void ClientSession::start() {
    if (thread_.joinable()) {
        throw std::logic_error("Session already started");
    }
    thread_ = std::thread(&ClientSession::run, this);
}

void ClientSession::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

pid_t ClientSession::pid() const noexcept {
    return pid_;
}

SessionStatus ClientSession::status() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void ClientSession::enqueue(Command command) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (status_.finished || stopping_) {
            return;
        }
        if (command.message.type == MessageType::RoundStart) {
            status_.move.reset();
        }
        commands_.push_back(std::move(command));
    }
    condition_.notify_one();
}

void ClientSession::startRound(std::uint64_t round, game::KidState state) {
    enqueue({{MessageType::RoundStart, id_, round}, state});
}

void ClientSession::sendResult(std::uint64_t round, game::KidState state) {
    const auto wireState =
        state == game::KidState::Alive ? WireKidState::Alive : WireKidState::Dead;
    enqueue({{MessageType::RoundResult, id_, round, 0, wireState}});
}

void ClientSession::finishGame(std::uint64_t round) {
    enqueue({{MessageType::GameOver, id_, round}});
}

void ClientSession::requestStop() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        commands_.clear();
    }
    condition_.notify_one();
}

void ClientSession::run() {
    std::string error;
    try {
        exchange();
    } catch (const std::exception& exception) {
        error = exception.what();
    }
    // Сначала освобождаем системные ресурсы, затем разрешаем контроллеру удалить сессию.
    connection_.reset();
    const std::lock_guard<std::mutex> lock(mutex_);
    status_.error = std::move(error);
    status_.finished = true;
}

void ClientSession::exchange() {
    connection_->completeHost();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
    }
    const auto ready = protocol::receiveMessage(*connection_);
    if (ready.type != MessageType::ClientReady || ready.clientId != id_) {
        throw protocol::ProtocolError("Expected ClientReady");
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.ready = true;
    }

    for (;;) {
        Command command{{MessageType::Waiting, id_}};
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait_for(lock, kWaitingInterval,
                                [this] { return stopping_ || !commands_.empty(); });
            if (stopping_) {
                command.message.type = MessageType::Disconnect;
            } else if (!commands_.empty()) {
                command = commands_.front();
                commands_.pop_front();
            }
        }
        // Только этот поток выполняет ввод-вывод данной сессии; мьютекс при этом не удерживается.
        protocol::sendMessage(*connection_, command.message);
        if (command.message.type == MessageType::GameOver ||
            command.message.type == MessageType::Disconnect) {
            return;
        }
        if (command.message.type != MessageType::RoundStart) {
            continue;
        }
        const auto move = protocol::receiveMessage(*connection_);
        if (move.clientId != id_) {
            throw protocol::ProtocolError("Unexpected client ID");
        }
        if (move.type == MessageType::Disconnect) {
            return;
        }
        if (move.type != MessageType::ClientMove ||
            move.roundNumber != command.message.roundNumber) {
            throw protocol::ProtocolError("Expected move for current round");
        }
        const int maxNumber =
            command.state == game::KidState::Alive ? game::kMaxAliveNumber : game::kMaxDeadNumber;
        if (move.number < game::kMinNumber || move.number > maxNumber) {
            throw protocol::ProtocolError("Invalid move for current kid state");
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        status_.move = move.number;
    }
}

} // namespace lab2::host
