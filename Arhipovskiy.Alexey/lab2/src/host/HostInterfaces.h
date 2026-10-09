#ifndef CHAT_HOST_INTERFACES_H
#define CHAT_HOST_INTERFACES_H

#include "chat/Message.h"

#include <string>
#include <vector>

namespace chat {

// Notifications from the host server; may be called from any thread
class HostListener {
public:
    virtual ~HostListener() = default;

    virtual void onStatus(const std::string& text) = 0;
    virtual void onParticipantsChanged(const std::vector<Participant>& participants) = 0;
    virtual void onMessage(const ChatMessage& message) = 0;
    virtual void onShutdownRequested() = 0;
};

// What the host's user interface can ask the server to do
class HostController {
public:
    virtual ~HostController() = default;

    virtual void sendMessage(int to, const std::string& text) = 0;
};

}

#endif // CHAT_HOST_INTERFACES_H
