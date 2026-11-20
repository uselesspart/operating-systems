#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace chat {
enum class Party : std::uint16_t { Everyone, Host, Client };
enum class PacketKind : std::uint16_t { Request = 1, Delivery };

struct Message {
    static constexpr std::size_t MaxText = 768;
    std::uint32_t magic = 0x524d4348;
    PacketKind kind = PacketKind::Request;
    Party sender = Party::Host;
    Party recipient = Party::Everyone;
    std::uint16_t reserved = 0;
    std::uint32_t length = 0;
    std::uint64_t sequence = 0;
    std::int64_t sentAtNs = 0;
    std::array<char, MaxText> text{};

    static Message compose(Party sender, Party recipient, std::uint64_t sequence,
                           std::int64_t sentAtNs, const std::string& text);
    void validate() const;
    bool visibleTo(Party viewer) const;
    std::string body() const;
};
static_assert(sizeof(Message) <= 4096, "A FIFO packet must fit in PIPE_BUF");
}
