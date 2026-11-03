#include "client/ConsoleUi.h"

#include "chat/Text.h"

namespace chat {

void ConsoleUi::setSelfId(int id)
{
    std::lock_guard lock(mutex_);
    selfId_ = id;
}

void ConsoleUi::onMessage(const ChatMessage& message)
{
    int self;
    {
        std::lock_guard lock(mutex_);
        self = selfId_;
    }
    print(text::formatMessage(message, self));
}

void ConsoleUi::onNotice(const std::string& notice)
{
    print("* " + notice);
}

void ConsoleUi::onDisconnected(const std::string& reason, bool lost)
{
    print(lost ? "* Связь с хостом потеряна: " + reason : "* Чат закрыт: " + reason);
}

void ConsoleUi::printConnected(pid_t hostPid, const char* connType, int id, const std::string& name)
{
    print("* Подключено к хосту " + std::to_string(hostPid) + " (" + connType + "), вы участник "
          + std::to_string(id) + " «" + name + "». Команды: /help");
}

void ConsoleUi::printParticipants(const std::vector<Participant>& participants)
{
    int self;
    {
        std::lock_guard lock(mutex_);
        self = selfId_;
    }
    std::string line = "* Участники:";
    for (const Participant& participant : participants) {
        line += " " + std::to_string(participant.id) + " " + participant.name;
        if (participant.id == self) {
            line += " (вы)";
        }
        line += ";";
    }
    print(line);
}

void ConsoleUi::printHelp()
{
    print("* текст            - сообщение всем");
    print("* /to <номер> текст - личное сообщение (то же: @<номер> текст)");
    print("* /who             - список участников");
    print("* /quit            - выйти из чата");
}

void ConsoleUi::printError(const std::string& error)
{
    print("! " + error);
}

void ConsoleUi::print(const std::string& line)
{
    std::lock_guard lock(mutex_);
    out_ << line << '\n' << std::flush;
}

}
