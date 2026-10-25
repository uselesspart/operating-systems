#include "chat/Protocol.h"

#include <utility>

namespace chat::protocol {

namespace {

class Writer {
public:
    void u8(std::uint8_t value) { bytes_.push_back(value); }

    void u32(std::uint32_t value) { putLittleEndian(value, 4); }
    void i32(std::int32_t value) { u32(static_cast<std::uint32_t>(value)); }
    void u64(std::uint64_t value) { putLittleEndian(value, 8); }
    void i64(std::int64_t value) { u64(static_cast<std::uint64_t>(value)); }

    void string(const std::string& value)
    {
        u32(static_cast<std::uint32_t>(value.size()));
        bytes_.insert(bytes_.end(), value.begin(), value.end());
    }

    Bytes take() { return std::move(bytes_); }

private:
    void putLittleEndian(std::uint64_t value, int size)
    {
        for (int i = 0; i < size; ++i) {
            bytes_.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }

    Bytes bytes_;
};

class Reader {
public:
    explicit Reader(const Bytes& bytes) : bytes_(bytes) {}

    std::uint8_t u8() { return static_cast<std::uint8_t>(getLittleEndian(1)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(getLittleEndian(4)); }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::uint64_t u64() { return getLittleEndian(8); }
    std::int64_t i64() { return static_cast<std::int64_t>(u64()); }

    std::string string()
    {
        const std::uint32_t size = u32();
        require(size);
        std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return value;
    }

    // Number of following elements, each taking at least minElementSize bytes
    std::uint32_t count(std::size_t minElementSize)
    {
        const std::uint32_t value = u32();
        if (value > (bytes_.size() - offset_) / minElementSize) {
            throw ProtocolError("element count exceeds frame size");
        }
        return value;
    }

    void finish() const
    {
        if (offset_ != bytes_.size()) {
            throw ProtocolError("trailing bytes in frame");
        }
    }

private:
    void require(std::size_t size) const
    {
        if (bytes_.size() - offset_ < size) {
            throw ProtocolError("frame is truncated");
        }
    }

    std::uint64_t getLittleEndian(int size)
    {
        require(static_cast<std::size_t>(size));
        std::uint64_t value = 0;
        for (int i = 0; i < size; ++i) {
            value |= static_cast<std::uint64_t>(bytes_[offset_ + i]) << (8 * i);
        }
        offset_ += static_cast<std::size_t>(size);
        return value;
    }

    const Bytes& bytes_;
    std::size_t offset_ = 0;
};

constexpr std::size_t kMinStringSize = 4;
constexpr std::size_t kMinOutgoingSize = 4 + 8 + kMinStringSize;
constexpr std::size_t kMinParticipantSize = 4 + kMinStringSize;
constexpr std::size_t kMinMessageSize = 8 + 8 + 4 + 4 + 3 * kMinStringSize;

template <typename Kind>
Kind checkedKind(std::uint8_t value, std::uint8_t first, std::uint8_t last)
{
    if (value < first || value > last) {
        throw ProtocolError("unknown frame kind " + std::to_string(value));
    }
    return static_cast<Kind>(value);
}

void writeMessage(Writer& out, const ChatMessage& message)
{
    out.u64(message.seq);
    out.i64(message.sentAtMs);
    out.i32(message.from);
    out.i32(message.to);
    out.string(message.fromName);
    out.string(message.toName);
    out.string(message.text);
}

ChatMessage readMessage(Reader& in)
{
    ChatMessage message;
    message.seq = in.u64();
    message.sentAtMs = in.i64();
    message.from = in.i32();
    message.to = in.i32();
    message.fromName = in.string();
    message.toName = in.string();
    message.text = in.string();
    return message;
}

}

Bytes encode(const Request& request)
{
    Writer out;
    out.u8(static_cast<std::uint8_t>(request.kind));
    out.string(request.name);
    out.u32(static_cast<std::uint32_t>(request.outgoing.size()));
    for (const OutgoingMessage& message : request.outgoing) {
        out.i32(message.to);
        out.i64(message.sentAtMs);
        out.string(message.text);
    }
    return out.take();
}

Bytes encode(const Response& response)
{
    Writer out;
    out.u8(static_cast<std::uint8_t>(response.kind));
    out.i32(response.clientId);
    out.string(response.reason);

    out.u8(response.participants ? 1 : 0);
    if (response.participants) {
        out.u32(static_cast<std::uint32_t>(response.participants->size()));
        for (const Participant& participant : *response.participants) {
            out.i32(participant.id);
            out.string(participant.name);
        }
    }

    out.u32(static_cast<std::uint32_t>(response.messages.size()));
    for (const ChatMessage& message : response.messages) {
        writeMessage(out, message);
    }

    out.u32(static_cast<std::uint32_t>(response.notices.size()));
    for (const std::string& notice : response.notices) {
        out.string(notice);
    }
    return out.take();
}

Request decodeRequest(const Bytes& bytes)
{
    Reader in(bytes);
    Request request;
    request.kind = checkedKind<RequestKind>(in.u8(), 1, 3);
    request.name = in.string();
    const std::uint32_t count = in.count(kMinOutgoingSize);
    request.outgoing.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        OutgoingMessage message;
        message.to = in.i32();
        message.sentAtMs = in.i64();
        message.text = in.string();
        request.outgoing.push_back(std::move(message));
    }
    in.finish();
    return request;
}

Response decodeResponse(const Bytes& bytes)
{
    Reader in(bytes);
    Response response;
    response.kind = checkedKind<ResponseKind>(in.u8(), 1, 3);
    response.clientId = in.i32();
    response.reason = in.string();

    if (in.u8() != 0) {
        const std::uint32_t count = in.count(kMinParticipantSize);
        std::vector<Participant> participants(count);
        for (Participant& participant : participants) {
            participant.id = in.i32();
            participant.name = in.string();
        }
        response.participants = std::move(participants);
    }

    const std::uint32_t messageCount = in.count(kMinMessageSize);
    response.messages.reserve(messageCount);
    for (std::uint32_t i = 0; i < messageCount; ++i) {
        response.messages.push_back(readMessage(in));
    }

    const std::uint32_t noticeCount = in.count(kMinStringSize);
    response.notices.reserve(noticeCount);
    for (std::uint32_t i = 0; i < noticeCount; ++i) {
        response.notices.push_back(in.string());
    }
    in.finish();
    return response;
}

}
