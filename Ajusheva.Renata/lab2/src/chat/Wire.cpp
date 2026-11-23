#include "chat/Wire.hpp"

namespace chatlab::wire {

void Writer::raw(std::uint64_t value, int width) {
    for (int index = 0; index < width; ++index) {
        out_.push_back(static_cast<std::uint8_t>(value >> (8 * index)));
    }
}

void Writer::u8(std::uint8_t value) {
    out_.push_back(value);
}

void Writer::u32(std::uint32_t value) {
    raw(value, 4);
}

void Writer::i32(std::int32_t value) {
    raw(static_cast<std::uint32_t>(value), 4);
}

void Writer::i64(std::int64_t value) {
    raw(static_cast<std::uint64_t>(value), 8);
}

void Writer::text(const std::string& value) {
    u32(static_cast<std::uint32_t>(value.size()));
    out_.insert(out_.end(), value.begin(), value.end());
}

void Reader::need(std::size_t size) const {
    if (in_.size() - at_ < size) {
        throw Error("unexpected end of packet");
    }
}

std::uint64_t Reader::raw(int width) {
    need(static_cast<std::size_t>(width));
    std::uint64_t value = 0;
    for (int index = 0; index < width; ++index) {
        value |= static_cast<std::uint64_t>(in_[at_++]) << (8 * index);
    }
    return value;
}

std::uint8_t Reader::u8() {
    return static_cast<std::uint8_t>(raw(1));
}

std::uint32_t Reader::u32() {
    return static_cast<std::uint32_t>(raw(4));
}

std::int32_t Reader::i32() {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(raw(4)));
}

std::int64_t Reader::i64() {
    return static_cast<std::int64_t>(raw(8));
}

std::string Reader::text(std::size_t limit) {
    const std::uint32_t size = u32();
    if (size > limit) {
        throw Error("text field is too long");
    }
    need(size);
    std::string value(in_.begin() + static_cast<std::ptrdiff_t>(at_),
                      in_.begin() + static_cast<std::ptrdiff_t>(at_ + size));
    at_ += size;
    return value;
}

std::size_t Reader::count(std::size_t limit) {
    const std::uint32_t value = u32();
    if (value > limit) {
        throw Error("too many items in packet");
    }
    return value;
}

}
