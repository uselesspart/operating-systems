#pragma once

#include <QString>
#include <optional>

namespace chat {

constexpr int everyone = -1;
constexpr int host = 0;
constexpr int maxTextBytes = 4000;

struct Message {
    int from;
    int to;
    quint64 number;
    qint64 sentNs;
    QString text;
};

QString participantName(int id);
QString encodeMessage(const Message& message);
std::optional<Message> decodeMessage(const QString& packet);
QString formatMessage(const Message& message);

} // namespace chat