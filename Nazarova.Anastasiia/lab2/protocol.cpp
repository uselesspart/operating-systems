#include "chat.hpp"

#include <stdexcept>
#include <tuple>

QJsonObject chatMessage(pid_t sender, pid_t recipient, const QString& text, quint64 serial) {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return {{"type", "chat"}, {"sender", sender}, {"recipient", recipient},
            {"text", text}, {"time", QString::number(now)}, {"serial", QString::number(serial)}};
}

void validateMessage(const QJsonObject& message, pid_t sender) {
    bool timeOk = false;
    bool serialOk = false;
    const qint64 time = message["time"].toString().toLongLong(&timeOk);
    message["serial"].toString().toULongLong(&serialOk);
    const int recipient = message["recipient"].toInt(-1);
    if (message["type"] != "chat" || message["sender"].toInt() != sender
        || recipient < 0 || message["recipient"].toDouble(-1) != recipient
        || message["text"].toString().trimmed().isEmpty() || !timeOk || time <= 0 || !serialOk) {
        throw std::runtime_error("Неверное сообщение");
    }
}

bool earlierMessage(const QJsonObject& left, const QJsonObject& right) {
    const auto key = [](const QJsonObject& message) {
        return std::make_tuple(message["time"].toString().toLongLong(),
                               message["sender"].toInt(), message["serial"].toString().toULongLong());
    };
    return key(left) < key(right);
}
