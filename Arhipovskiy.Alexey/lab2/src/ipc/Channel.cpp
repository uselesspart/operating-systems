#include "ipc/Channel.h"

#include "ipc/Names.h"

#include <array>

namespace chat {

namespace {

constexpr std::uint32_t kFrameMagic = 0x54414843; // "CHAT"
constexpr std::size_t kHeaderSize = 8;

NamedSemaphore makeSemaphore(const ConnId& id, const char* direction, Channel::Side side)
{
    const std::string name = names::semaphoreName(id, direction);
    return side == Channel::Side::Host ? NamedSemaphore::create(name) : NamedSemaphore::open(name);
}

void putU32(std::uint8_t* out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out[i] = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

std::uint32_t getU32(const std::uint8_t* in)
{
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    }
    return value;
}

}

const char* describe(ChannelStatus status)
{
    switch (status) {
    case ChannelStatus::Ok:
        return "ok";
    case ChannelStatus::Timeout:
        return "timeout";
    case ChannelStatus::Closed:
        return "connection closed";
    case ChannelStatus::Corrupted:
        return "corrupted frame";
    }
    return "unknown";
}

Channel::Channel(const ConnId& id, Side side)
    : toHost_(makeSemaphore(id, names::kToHost, side)), toClient_(makeSemaphore(id, names::kToClient, side)),
      conn_(id, side == Side::Host), incoming_(side == Side::Host ? toHost_ : toClient_),
      outgoing_(side == Side::Host ? toClient_ : toHost_)
{
}

ChannelStatus Channel::send(const Bytes& payload)
{
    if (payload.size() > kMaxFrameSize) {
        return ChannelStatus::Corrupted;
    }
    Bytes frame(kHeaderSize + payload.size());
    putU32(frame.data(), kFrameMagic);
    putU32(frame.data() + 4, static_cast<std::uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), frame.begin() + kHeaderSize);

    if (!conn_.Write(frame.data(), frame.size()) || !outgoing_.post()) {
        return ChannelStatus::Closed;
    }
    return ChannelStatus::Ok;
}

ChannelStatus Channel::receive(Bytes& payload, std::chrono::milliseconds timeout)
{
    if (!incoming_.waitFor(timeout)) {
        return ChannelStatus::Timeout;
    }
    std::array<std::uint8_t, kHeaderSize> header{};
    if (!conn_.Read(header.data(), header.size())) {
        return ChannelStatus::Closed;
    }
    const std::uint32_t size = getU32(header.data() + 4);
    if (getU32(header.data()) != kFrameMagic || size > kMaxFrameSize) {
        return ChannelStatus::Corrupted;
    }
    payload.resize(size);
    if (size > 0 && !conn_.Read(payload.data(), size)) {
        return ChannelStatus::Closed;
    }
    return ChannelStatus::Ok;
}

}
