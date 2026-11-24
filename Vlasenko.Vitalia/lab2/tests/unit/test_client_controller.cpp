#include "client/client_controller.hpp"
#include "protocol/protocol.hpp"

#include <cstring>
#include <gtest/gtest.h>
#include <initializer_list>
#include <sstream>
#include <vector>

namespace lab2::client {
namespace {
using game::KidState;
using protocol::Message;
using protocol::MessageType;
using protocol::WireKidState;
constexpr game::ClientId kId = 7;

class ScriptedConnection final : public ipc::IConnection {
public:
    explicit ScriptedConnection(std::initializer_list<Message> messages) {
        for (const auto& message : messages) {
            incoming_.push_back(protocol::encode(message));
        }
    }

    void read(void* buffer, std::size_t count) override {
        if (count != protocol::kFrameSize || position_ == incoming_.size()) {
            throw std::runtime_error("Unexpected read");
        }
        std::memcpy(buffer, incoming_[position_++].data(), count);
    }

    void write(const void* buffer, std::size_t count) override {
        if (count != protocol::kFrameSize) {
            throw std::runtime_error("Unexpected write size");
        }
        protocol::Frame frame{};
        std::memcpy(frame.data(), buffer, count);
        outgoing.push_back(protocol::decode(frame));
    }

    std::vector<Message> outgoing;

private:
    std::vector<protocol::Frame> incoming_;
    std::size_t position_{};
};

int fixedMove(KidState) {
    return 1;
}

TEST(ClientControllerTest, AppliesHostStateBeforeGeneratingNextMove) {
    ScriptedConnection connection({{MessageType::Waiting, kId},
                                   {MessageType::RoundStart, kId, 4},
                                   {MessageType::Waiting, kId},
                                   {MessageType::RoundResult, kId, 4, 0, WireKidState::Dead},
                                   {MessageType::RoundStart, kId, 5},
                                   {MessageType::RoundResult, kId, 5, 0, WireKidState::Dead},
                                   {MessageType::GameOver, kId, 5}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    std::vector<KidState> states;
    controller.play(connection, kId, [&](KidState state) {
        states.push_back(state);
        return state == KidState::Alive ? 100 : 50;
    });
    EXPECT_EQ(states, (std::vector<KidState>{KidState::Alive, KidState::Dead}));
    ASSERT_EQ(connection.outgoing.size(), 3U);
    EXPECT_EQ(connection.outgoing[0].type, MessageType::ClientReady);
    EXPECT_EQ(connection.outgoing[1].type, MessageType::ClientMove);
    EXPECT_EQ(connection.outgoing[1].roundNumber, 4U);
    EXPECT_EQ(connection.outgoing[1].number, 100U);
    EXPECT_EQ(connection.outgoing[2].number, 50U);
    EXPECT_EQ(connection.outgoing[2].roundNumber, 5U);
    EXPECT_NE(output.str().find("Game over"), std::string::npos);
}

TEST(ClientControllerTest, RevivedKidUsesAliveRange) {
    ScriptedConnection connection({{MessageType::RoundStart, kId, 1},
                                   {MessageType::RoundResult, kId, 1, 0, WireKidState::Dead},
                                   {MessageType::RoundStart, kId, 2},
                                   {MessageType::RoundResult, kId, 2, 0, WireKidState::Alive},
                                   {MessageType::RoundStart, kId, 3},
                                   {MessageType::Disconnect, kId}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    std::vector<KidState> states;
    controller.play(connection, kId, [&](KidState state) {
        states.push_back(state);
        return state == KidState::Alive ? 100 : 50;
    });
    EXPECT_EQ(states, (std::vector<KidState>{KidState::Alive, KidState::Dead, KidState::Alive}));
    ASSERT_EQ(connection.outgoing.size(), 4U);
    EXPECT_EQ(connection.outgoing.back().number, 100U);
}

TEST(ClientControllerTest, RejectsWrongClientId) {
    ScriptedConnection connection({{MessageType::RoundStart, kId + 1, 1}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    EXPECT_THROW(controller.play(connection, kId, fixedMove), protocol::ProtocolError);
}

TEST(ClientControllerTest, RejectsResultForWrongRound) {
    ScriptedConnection connection({{MessageType::RoundStart, kId, 2},
                                   {MessageType::RoundResult, kId, 1, 0, WireKidState::Alive}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    EXPECT_THROW(controller.play(connection, kId, fixedMove), protocol::ProtocolError);
}

TEST(ClientControllerTest, RejectsMoveOutsideDeadRange) {
    ScriptedConnection connection({{MessageType::RoundStart, kId, 1},
                                   {MessageType::RoundResult, kId, 1, 0, WireKidState::Dead},
                                   {MessageType::RoundStart, kId, 2}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    EXPECT_THROW(controller.play(connection, kId, [](KidState) { return 51; }), std::logic_error);
    EXPECT_EQ(connection.outgoing.size(), 2U);
}

TEST(ClientControllerTest, AcceptsGameOverForLateClient) {
    ScriptedConnection connection({{MessageType::Waiting, kId}, {MessageType::GameOver, kId, 8}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    EXPECT_NO_THROW(controller.play(connection, kId, fixedMove));
    EXPECT_EQ(connection.outgoing.size(), 1U);
}

TEST(ClientControllerTest, HandlesDisconnectWhileWaitingForResult) {
    ScriptedConnection connection({{MessageType::RoundStart, kId, 1},
                                   {MessageType::Waiting, kId},
                                   {MessageType::Disconnect, kId}});
    std::ostringstream output;
    common::Logger logger(output);
    ClientController controller(logger);
    EXPECT_NO_THROW(controller.play(connection, kId, fixedMove));
    EXPECT_NE(output.str().find("Host disconnected"), std::string::npos);
}

} // namespace
} // namespace lab2::client
