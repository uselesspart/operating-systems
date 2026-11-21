#include "link/Link.hpp"

#include "link/Names.hpp"

#include <array>

namespace chatlab {

namespace {

constexpr std::uint32_t kSignature = 0x4C324348;
constexpr std::size_t kHeaderBytes = 8;

GlobalSemaphore openSemaphore(const ConnKey& key, const char* way, Link::Side side) {
    const std::string name = names::semaphoreName(key, way);
    return side == Link::Side::Host ? GlobalSemaphore::create(name) : GlobalSemaphore::attach(name);
}

void storeWord(std::uint8_t* target, std::uint32_t value) {
    for (int shift = 0; shift < 4; ++shift) {
        target[shift] = static_cast<std::uint8_t>(value >> (8 * shift));
    }
}

std::uint32_t loadWord(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (int shift = 0; shift < 4; ++shift) {
        value |= static_cast<std::uint32_t>(source[shift]) << (8 * shift);
    }
    return value;
}

}

const char* statusText(LinkStatus status) {
    switch (status) {
    case LinkStatus::Ok:
        return "ok";
    case LinkStatus::TimedOut:
        return "тайм-аут ожидания";
    case LinkStatus::Broken:
        return "канал закрыт";
    case LinkStatus::Malformed:
        return "повреждённые данные";
    }
    return "?";
}

Link::Link(const ConnKey& key, Side side)
    : up_(openSemaphore(key, names::kUp, side)),
      down_(openSemaphore(key, names::kDown, side)),
      conn_(key, side == Side::Host),
      inbound_(side == Side::Host ? up_ : down_),
      outbound_(side == Side::Host ? down_ : up_) {}

LinkStatus Link::send(const Bytes& payload) {
    if (payload.size() > kFrameLimit) {
        return LinkStatus::Malformed;
    }
    Bytes frame(kHeaderBytes + payload.size());
    storeWord(frame.data(), kSignature);
    storeWord(frame.data() + 4, static_cast<std::uint32_t>(payload.size()));
    std::copy(payload.begin(), payload.end(), frame.begin() + kHeaderBytes);
    if (!conn_.Write(frame.data(), frame.size())) {
        return LinkStatus::Broken;
    }
    return outbound_.post() ? LinkStatus::Ok : LinkStatus::Broken;
}

LinkStatus Link::receive(Bytes& payload, std::chrono::milliseconds timeout) {
    if (!inbound_.waitFor(timeout)) {
        return LinkStatus::TimedOut;
    }
    std::array<std::uint8_t, kHeaderBytes> header{};
    if (!conn_.Read(header.data(), header.size())) {
        return LinkStatus::Broken;
    }
    const std::uint32_t size = loadWord(header.data() + 4);
    if (loadWord(header.data()) != kSignature || size > kFrameLimit) {
        return LinkStatus::Malformed;
    }
    payload.assign(size, 0);
    if (size > 0 && !conn_.Read(payload.data(), size)) {
        return LinkStatus::Broken;
    }
    return LinkStatus::Ok;
}

}
