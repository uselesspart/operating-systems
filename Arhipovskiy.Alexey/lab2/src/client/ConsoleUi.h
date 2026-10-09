#ifndef CHAT_CONSOLE_UI_H
#define CHAT_CONSOLE_UI_H

#include "client/ChatClient.h"

#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace chat {

// Terminal front end of the client: prints chat events, one line each
class ConsoleUi : public ClientListener {
public:
    explicit ConsoleUi(std::ostream& out) : out_(out) {}

    void setSelfId(int id);

    void onMessage(const ChatMessage& message) override;
    void onNotice(const std::string& notice) override;
    void onDisconnected(const std::string& reason, bool lost) override;

    void printConnected(pid_t hostPid, const char* connType, int id, const std::string& name);
    void printParticipants(const std::vector<Participant>& participants);
    void printHelp();
    void printError(const std::string& error);

private:
    void print(const std::string& line);

    std::ostream& out_;
    std::mutex mutex_;
    int selfId_ = 0;
};

}

#endif // CHAT_CONSOLE_UI_H
