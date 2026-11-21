#include "check.hpp"

#include "chat/Render.hpp"

using namespace chatlab;

namespace {

Post sample(int from, int to) {
    Post post;
    post.sentAt = 0;
    post.from = from;
    post.to = to;
    post.fromName = from == 0 ? "Хост" : "Алиса";
    post.toName = to == kEveryone ? "" : "Борис";
    post.text = "текст";
    return post;
}

std::string tail(const std::string& line) {
    return line.substr(line.find(']') + 2);
}

}

TEST(render_public_message) {
    CHECK_EQ(tail(render::postLine(sample(1, kEveryone), 2)), std::string("Алиса: текст"));
}

TEST(render_private_message_for_each_viewer) {
    const Post post = sample(1, 2);
    CHECK_EQ(tail(render::postLine(post, 1)), std::string("вы -> Борис (лично): текст"));
    CHECK_EQ(tail(render::postLine(post, 2)), std::string("Алиса -> вам (лично): текст"));
    CHECK_EQ(tail(render::postLine(post, 0)), std::string("Алиса -> Борис (лично): текст"));
}

TEST(render_roster_line) {
    CHECK_EQ(render::rosterLine({{0, "Хост"}, {3, "Вера"}}), std::string("0 Хост, 3 Вера"));
    CHECK_EQ(render::rosterLine({}), std::string(""));
}
