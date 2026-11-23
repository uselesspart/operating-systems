#include "check.hpp"

#include "chat/Limits.hpp"
#include "chat/Packets.hpp"

using namespace chatlab;

TEST(packets_request_round_trip) {
    packets::Request request;
    request.ask = packets::Ask::Hello;
    request.name = "Алиса";
    request.drafts.push_back({kEveryone, 1700000000123, "привет"});
    request.drafts.push_back({3, 1700000000456, "лично"});

    const packets::Request back = packets::unpackRequest(packets::pack(request));
    CHECK(back.ask == packets::Ask::Hello);
    CHECK_EQ(back.name, std::string("Алиса"));
    CHECK_EQ(back.drafts.size(), std::size_t(2));
    CHECK_EQ(back.drafts[0].to, kEveryone);
    CHECK_EQ(back.drafts[1].to, 3);
    CHECK_EQ(back.drafts[1].sentAt, Millis(1700000000456));
    CHECK_EQ(back.drafts[1].text, std::string("лично"));
}

TEST(packets_reply_round_trip) {
    packets::Reply reply;
    reply.answer = packets::Answer::Update;
    reply.you = 4;
    reply.yourName = "Борис";
    reply.hasRoster = true;
    reply.roster = {{0, "Хост"}, {4, "Борис"}};
    Post post;
    post.seq = 77;
    post.sentAt = 5;
    post.from = 0;
    post.to = 4;
    post.fromName = "Хост";
    post.toName = "Борис";
    post.text = "текст";
    reply.posts.push_back(post);
    reply.notes = {"кто-то вошёл"};

    const packets::Reply back = packets::unpackReply(packets::pack(reply));
    CHECK(back.answer == packets::Answer::Update);
    CHECK_EQ(back.you, 4);
    CHECK_EQ(back.yourName, std::string("Борис"));
    CHECK(back.hasRoster);
    CHECK(back.roster == reply.roster);
    CHECK_EQ(back.posts.size(), std::size_t(1));
    CHECK_EQ(back.posts[0].seq, std::uint64_t(77));
    CHECK_EQ(back.posts[0].toName, std::string("Борис"));
    CHECK(back.posts[0].isPrivate());
    CHECK_EQ(back.notes, std::vector<std::string>{"кто-то вошёл"});
}

TEST(packets_reply_without_roster_keeps_it_absent) {
    packets::Reply reply;
    reply.answer = packets::Answer::Closed;
    reply.reason = "хост завершает работу";
    const packets::Reply back = packets::unpackReply(packets::pack(reply));
    CHECK(back.answer == packets::Answer::Closed);
    CHECK(!back.hasRoster);
    CHECK(back.roster.empty());
    CHECK_EQ(back.reason, std::string("хост завершает работу"));
}

TEST(packets_truncated_input_is_rejected) {
    packets::Request request;
    request.drafts.push_back({kEveryone, 1, "abc"});
    wire::Bytes bytes = packets::pack(request);
    for (std::size_t cut = 0; cut < bytes.size(); ++cut) {
        const wire::Bytes part(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(cut));
        CHECK_THROWS(packets::unpackRequest(part), wire::Error);
    }
}

TEST(packets_garbage_and_wrong_tags_are_rejected) {
    CHECK_THROWS(packets::unpackRequest(wire::Bytes{0x01, 0x02}), wire::Error);
    CHECK_THROWS(packets::unpackReply(packets::pack(packets::Request{})), wire::Error);
    CHECK_THROWS(packets::unpackRequest(packets::pack(packets::Reply{})), wire::Error);

    wire::Bytes extra = packets::pack(packets::Request{});
    extra.push_back(0);
    CHECK_THROWS(packets::unpackRequest(extra), wire::Error);
}

TEST(packets_too_many_drafts_are_rejected) {
    packets::Request request;
    for (std::size_t index = 0; index <= kDraftsPerRequest; ++index) {
        request.drafts.push_back({kEveryone, 1, "x"});
    }
    CHECK_THROWS(packets::unpackRequest(packets::pack(request)), wire::Error);
}

TEST(packets_full_reply_fits_into_one_frame) {
    packets::Reply reply;
    reply.hasRoster = true;
    for (std::size_t index = 0; index <= kMemberLimit; ++index) {
        reply.roster.push_back({static_cast<int>(index), std::string(kNameLimit, 'n')});
    }
    for (std::size_t index = 0; index < kPostsPerReply; ++index) {
        Post post;
        post.fromName = std::string(kNameLimit, 'a');
        post.toName = std::string(kNameLimit, 'b');
        post.text = std::string(kTextLimit, 't');
        reply.posts.push_back(post);
    }
    for (std::size_t index = 0; index < kNotesPerReply; ++index) {
        reply.notes.push_back(std::string(kNoteLimit, 'z'));
    }
    CHECK(packets::pack(reply).size() < 56 * 1024);
}
