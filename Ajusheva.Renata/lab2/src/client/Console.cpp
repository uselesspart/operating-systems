#include "client/Console.hpp"

#include "chat/Render.hpp"

namespace chatlab {

void Console::greeting(pid_t host, const char* kind, int id, const std::string& name) {
    out_ << "Подключено к хосту " << host << " (тип связи " << kind << "). Ваш номер " << id
         << ", имя «" << name << "». Команды: /help" << std::endl;
}

void Console::show(const packets::Reply& reply) {
    if (reply.hasRoster) {
        roster_ = reply.roster;
        out_ << "* участники: " << render::rosterLine(roster_) << '\n';
    }
    for (const std::string& note : reply.notes) {
        out_ << "* " << note << '\n';
    }
    for (const Post& post : reply.posts) {
        out_ << render::postLine(post, viewer_) << '\n';
    }
    out_.flush();
}

void Console::roster() {
    out_ << "* участники: " << render::rosterLine(roster_) << std::endl;
}

void Console::help() {
    out_ << "  текст            сообщение всем\n"
         << "  /to N текст      личное сообщение участнику N (хост - 0)\n"
         << "  @N текст         то же самое\n"
         << "  /who             список участников\n"
         << "  /quit            выйти (или Ctrl+D, Ctrl+C)" << std::endl;
}

void Console::problem(const std::string& text) {
    out_ << "! " << text << std::endl;
}

void Console::info(const std::string& text) {
    out_ << "* " << text << std::endl;
}

}
