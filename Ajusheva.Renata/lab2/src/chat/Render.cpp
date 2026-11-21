#include "chat/Render.hpp"

namespace chatlab::render {

std::string postLine(const Post& post, int viewer) {
    std::string line = "[" + clockText(post.sentAt) + "] ";
    if (!post.isPrivate()) {
        return line + post.fromName + ": " + post.text;
    }
    const std::string sender = post.from == viewer ? "вы" : post.fromName;
    const std::string receiver = post.to == viewer ? "вам" : post.toName;
    return line + sender + " -> " + receiver + " (лично): " + post.text;
}

std::string rosterLine(const std::vector<Member>& roster) {
    std::string line;
    for (const Member& member : roster) {
        if (!line.empty()) {
            line += ", ";
        }
        line += std::to_string(member.id) + " " + member.name;
    }
    return line;
}

}
