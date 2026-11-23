#include "chat/Packets.hpp"

#include "chat/Limits.hpp"

namespace chatlab::packets {

namespace {

constexpr std::uint8_t kRequestTag = 0x51;
constexpr std::uint8_t kReplyTag = 0x52;
constexpr std::size_t kNameBytes = kNameLimit * 2;

void putPost(wire::Writer& out, const Post& post) {
    out.i64(static_cast<std::int64_t>(post.seq));
    out.i64(post.sentAt);
    out.i32(post.from);
    out.i32(post.to);
    out.text(post.fromName);
    out.text(post.toName);
    out.text(post.text);
}

Post getPost(wire::Reader& in) {
    Post post;
    post.seq = static_cast<std::uint64_t>(in.i64());
    post.sentAt = in.i64();
    post.from = in.i32();
    post.to = in.i32();
    post.fromName = in.text(kNameBytes);
    post.toName = in.text(kNameBytes);
    post.text = in.text(kTextLimit);
    return post;
}

void expectEnd(const wire::Reader& in) {
    if (!in.finished()) {
        throw wire::Error("extra bytes after packet");
    }
}

}

wire::Bytes pack(const Request& request) {
    wire::Writer out;
    out.u8(kRequestTag);
    out.u8(static_cast<std::uint8_t>(request.ask));
    out.text(request.name);
    out.u32(static_cast<std::uint32_t>(request.drafts.size()));
    for (const Draft& draft : request.drafts) {
        out.i32(draft.to);
        out.i64(draft.sentAt);
        out.text(draft.text);
    }
    return out.take();
}

wire::Bytes pack(const Reply& reply) {
    wire::Writer out;
    out.u8(kReplyTag);
    out.u8(static_cast<std::uint8_t>(reply.answer));
    out.i32(reply.you);
    out.text(reply.yourName);
    out.text(reply.reason);
    out.u8(reply.hasRoster ? 1 : 0);
    if (reply.hasRoster) {
        out.u32(static_cast<std::uint32_t>(reply.roster.size()));
        for (const Member& member : reply.roster) {
            out.i32(member.id);
            out.text(member.name);
        }
    }
    out.u32(static_cast<std::uint32_t>(reply.posts.size()));
    for (const Post& post : reply.posts) {
        putPost(out, post);
    }
    out.u32(static_cast<std::uint32_t>(reply.notes.size()));
    for (const std::string& note : reply.notes) {
        out.text(note);
    }
    return out.take();
}

Request unpackRequest(const wire::Bytes& bytes) {
    wire::Reader in(bytes);
    if (in.u8() != kRequestTag) {
        throw wire::Error("not a request");
    }
    Request request;
    const std::uint8_t ask = in.u8();
    if (ask < 1 || ask > 3) {
        throw wire::Error("unknown request kind");
    }
    request.ask = static_cast<Ask>(ask);
    request.name = in.text(kNameBytes * 4);
    const std::size_t drafts = in.count(kDraftsPerRequest);
    for (std::size_t index = 0; index < drafts; ++index) {
        Draft draft;
        draft.to = in.i32();
        draft.sentAt = in.i64();
        draft.text = in.text(kTextLimit * 4);
        request.drafts.push_back(std::move(draft));
    }
    expectEnd(in);
    return request;
}

Reply unpackReply(const wire::Bytes& bytes) {
    wire::Reader in(bytes);
    if (in.u8() != kReplyTag) {
        throw wire::Error("not a reply");
    }
    Reply reply;
    const std::uint8_t answer = in.u8();
    if (answer < 1 || answer > 3) {
        throw wire::Error("unknown reply kind");
    }
    reply.answer = static_cast<Answer>(answer);
    reply.you = in.i32();
    reply.yourName = in.text(kNameBytes);
    reply.reason = in.text(kNoteLimit);
    reply.hasRoster = in.u8() != 0;
    if (reply.hasRoster) {
        const std::size_t members = in.count(kMemberLimit + 1);
        for (std::size_t index = 0; index < members; ++index) {
            Member member;
            member.id = in.i32();
            member.name = in.text(kNameBytes);
            reply.roster.push_back(std::move(member));
        }
    }
    const std::size_t posts = in.count(kPostsPerReply);
    for (std::size_t index = 0; index < posts; ++index) {
        reply.posts.push_back(getPost(in));
    }
    const std::size_t notes = in.count(kNotesPerReply);
    for (std::size_t index = 0; index < notes; ++index) {
        reply.notes.push_back(in.text(kNoteLimit));
    }
    expectEnd(in);
    return reply;
}

}
