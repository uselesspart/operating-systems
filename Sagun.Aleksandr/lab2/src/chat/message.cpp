#include "message.hpp"

#include <QByteArray>
#include <QStringList>

namespace chat {

QString participantName(int id)
{
    return id == host ? QStringLiteral("Хост") : QStringLiteral("Клиент %1").arg(id);
}

QString encodeMessage(const Message& message)
{
    // Первая строка содержит числа, всё после первого перевода строки — текст.
    return QStringLiteral("%1 %2 %3 %4\n").arg(message.from).arg(message.to)
        .arg(message.number).arg(message.sentNs) + message.text;
}

std::optional<Message> decodeMessage(const QString& packet)
{
    const auto end = packet.indexOf(QLatin1Char('\n'));
    if (end < 0) return std::nullopt;
    const auto fields = packet.left(end).split(QLatin1Char(' '));
    if (fields.size() != 4) return std::nullopt;

    bool fromOk, toOk, numberOk, timeOk;
    const int from = fields[0].toInt(&fromOk);
    const int to = fields[1].toInt(&toOk);
    const auto number = fields[2].toULongLong(&numberOk);
    const auto sentNs = fields[3].toLongLong(&timeOk);
    const QString text = packet.mid(end + 1);
    if (!fromOk || from < host || !toOk || to < everyone || !numberOk ||
        !timeOk || sentNs < 0 || text.isEmpty() || text.toUtf8().size() > maxTextBytes) {
        return std::nullopt;
    }
    return Message{from, to, number, sentNs, text};
}

QString formatMessage(const Message& message)
{
    QString label = participantName(message.from);
    if (message.to != everyone) {
        label += QStringLiteral(" → %1 (лично)").arg(participantName(message.to));
    }
    return QStringLiteral("#%1 %2: ").arg(message.number).arg(label) + message.text;
}

} // namespace chat