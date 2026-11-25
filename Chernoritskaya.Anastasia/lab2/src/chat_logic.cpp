#include "chat_logic.h"
#include <algorithm>

void ChatLogic::addParticipant(const std::string& name) {
    if (std::find(m_participants.begin(), m_participants.end(), name) == m_participants.end()) {
        m_participants.push_back(name);
        m_lastActivity[name] = std::chrono::system_clock::now();
    }
}

void ChatLogic::removeParticipant(const std::string& name) {
    m_participants.erase(
        std::remove(m_participants.begin(), m_participants.end(), name),
        m_participants.end());
    m_lastActivity.erase(name);
}

std::vector<std::string> ChatLogic::routeMessage(const ChatMessage& msg) {
    std::vector<std::string> targets;
    if (msg.recipient.empty()) {
        for (const auto& p : m_participants)
            if (p != msg.sender) targets.push_back(p);
    } else if (std::find(m_participants.begin(), m_participants.end(), msg.recipient)
               != m_participants.end()) {
        targets.push_back(msg.recipient);
    }
    return targets;
}

bool ChatLogic::isInactive(const std::string& name,
                           std::chrono::system_clock::time_point now) const {
    if (name == "host") return false;
    auto it = m_lastActivity.find(name);
    if (it == m_lastActivity.end()) return false;
    return std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count() > 60;
}

void ChatLogic::updateActivity(const std::string& name,
                               std::chrono::system_clock::time_point now) {
    m_lastActivity[name] = now;
}

std::vector<std::string> ChatLogic::getTimedOutParticipants(
    std::chrono::system_clock::time_point now) const {
    std::vector<std::string> result;
    for (const auto& kv : m_lastActivity)
        if (isInactive(kv.first, now)) result.push_back(kv.first);
    return result;
}