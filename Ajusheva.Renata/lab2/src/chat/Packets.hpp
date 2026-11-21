#pragma once

#include "chat/Post.hpp"
#include "chat/Wire.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace chatlab::packets {

enum class Ask : std::uint8_t { Hello = 1, Poll = 2, Bye = 3 };

struct Request {
    Ask ask = Ask::Poll;
    std::string name;
    std::vector<Draft> drafts;
};

enum class Answer : std::uint8_t { Welcome = 1, Update = 2, Closed = 3 };

struct Reply {
    Answer answer = Answer::Update;
    int you = 0;
    std::string yourName;
    std::string reason;
    bool hasRoster = false;
    std::vector<Member> roster;
    std::vector<Post> posts;
    std::vector<std::string> notes;
};

wire::Bytes pack(const Request& request);
wire::Bytes pack(const Reply& reply);

Request unpackRequest(const wire::Bytes& bytes);
Reply unpackReply(const wire::Bytes& bytes);

}
