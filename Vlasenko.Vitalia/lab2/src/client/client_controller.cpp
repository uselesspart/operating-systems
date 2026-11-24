#include "client/client_controller.hpp"

#include "handshake/handshake_client.hpp"
#include "ipc/connection_factory.hpp"
#include "protocol/protocol.hpp"

#include <random>
#include <string>

namespace lab2::client {
namespace {
using protocol::MessageType;
using protocol::WireKidState;
} // namespace

ClientController::ClientController(common::Logger& logger) : logger_(logger) {
}

void ClientController::run(pid_t hostPid) {
    const auto id = handshake::HandshakeClient{}.connect(hostPid);
    auto connection = ipc::connectClientConnection(hostPid, id);
    logger_.info("Connected as client " + std::to_string(id));
    std::mt19937 random(std::random_device{}());
    play(*connection, id, [&random](game::KidState state) {
        const int maxNumber =
            state == game::KidState::Alive ? game::kMaxAliveNumber : game::kMaxDeadNumber;
        return std::uniform_int_distribution<int>(game::kMinNumber, maxNumber)(random);
    });
}

void ClientController::play(ipc::IConnection& connection, game::ClientId clientId,
                            const MoveGenerator& generateMove) {
    if (clientId == 0 || !generateMove) {
        throw std::invalid_argument("Invalid client configuration");
    }
    auto state = game::KidState::Alive;
    std::uint64_t lastRound = 0;
    bool awaitingResult = false;
    protocol::sendMessage(connection, {MessageType::ClientReady, clientId});

    for (;;) {
        const auto message = protocol::receiveMessage(connection);
        if (message.clientId != clientId) {
            throw protocol::ProtocolError("Unexpected client ID");
        }
        switch (message.type) {
        case MessageType::Waiting:
            // Хост может ожидать других клиентов; каждый кадр подтверждает, что связь жива.
            break;
        case MessageType::RoundStart: {
            if (awaitingResult || message.roundNumber <= lastRound) {
                throw protocol::ProtocolError("Unexpected round start");
            }
            lastRound = message.roundNumber;
            const int number = generateMove(state);
            const int maxNumber =
                state == game::KidState::Alive ? game::kMaxAliveNumber : game::kMaxDeadNumber;
            if (number < game::kMinNumber || number > maxNumber) {
                throw std::logic_error("Move generator returned an invalid number");
            }
            protocol::sendMessage(connection, {MessageType::ClientMove, clientId, lastRound,
                                               static_cast<std::uint16_t>(number)});
            logger_.info("Round " + std::to_string(lastRound) + ": number " +
                         std::to_string(number));
            awaitingResult = true;
            break;
        }
        case MessageType::RoundResult:
            if (!awaitingResult || message.roundNumber != lastRound) {
                throw protocol::ProtocolError("Unexpected round result");
            }
            state =
                message.state == WireKidState::Alive ? game::KidState::Alive : game::KidState::Dead;
            awaitingResult = false;
            logger_.info(state == game::KidState::Alive ? "State: Alive" : "State: Dead");
            break;
        case MessageType::GameOver:
            if (awaitingResult || message.roundNumber < lastRound) {
                throw protocol::ProtocolError("Unexpected game over");
            }
            logger_.info("Game over");
            return;
        case MessageType::Disconnect:
            logger_.info("Host disconnected");
            return;
        default:
            throw protocol::ProtocolError("Unexpected message from host");
        }
    }
}

} // namespace lab2::client
