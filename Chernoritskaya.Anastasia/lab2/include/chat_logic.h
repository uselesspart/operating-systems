#pragma once
#include <string>
#include <vector>
#include <map>
#include <chrono>

struct ChatMessage {
    std::string sender;
    std::string recipient;   // пусто = broadcast
    std::string content;
    std::chrono::system_clock::time_point timestamp;
};

class ChatLogic {
public:
    void addParticipant(const std::string& name);
    void removeParticipant(const std::string& name);
    std::vector<std::string> routeMessage(const ChatMessage& msg);
    void updateActivity(const std::string& name,
                        std::chrono::system_clock::time_point now);
    std::vector<std::string> getTimedOutParticipants(
        std::chrono::system_clock::time_point now) const;
    const std::vector<std::string>& participants() const { return m_participants; }

private:
    bool isInactive(const std::string& name,
                    std::chrono::system_clock::time_point now) const;
    std::vector<std::string> m_participants;
    std::map<std::string, std::chrono::system_clock::time_point> m_lastActivity;
};