#ifndef CHAT_PROTOCOL_H
#define CHAT_PROTOCOL_H

#include "chat/Message.h"
#include "util/Bytes.h"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// One request from the client is always answered by one response from the host
namespace chat::protocol {

enum class RequestKind : std::uint8_t { Join = 1, Poll = 2, Leave = 3 };

struct Request {
    RequestKind kind = RequestKind::Poll;
    std::string name;
    std::vector<OutgoingMessage> outgoing;
};

enum class ResponseKind : std::uint8_t { Welcome = 1, Update = 2, Bye = 3 };

struct Response {
    ResponseKind kind = ResponseKind::Update;
    int clientId = 0;
    std::string reason;
    std::optional<std::vector<Participant>> participants;
    std::vector<ChatMessage> messages;
    std::vector<std::string> notices;
};

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Bytes encode(const Request& request);
Bytes encode(const Response& response);

// Throw ProtocolError on malformed input
Request decodeRequest(const Bytes& bytes);
Response decodeResponse(const Bytes& bytes);

}

#endif // CHAT_PROTOCOL_H
