#include "connection.hpp"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>
#include <cerrno>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

Conn::Conn(pid_t host, pid_t client, bool create)
    : directory_(hostDirectory(host) / std::to_string(client), create),
      sendTurn_(resourceName(host, client, create ? "h_free" : "c_free"), create, 1),
      sendReady_(resourceName(host, client, create ? "h_ready" : "c_ready"), create, 0),
      receiveTurn_(resourceName(host, client, create ? "c_free" : "h_free"), create, 1),
      receiveReady_(resourceName(host, client, create ? "c_ready" : "h_ready"), create, 0) {
    Prepare(host, create);
}

void Conn::Connected() { pending_.clear(); }

void Conn::readBytes(void* data, size_t size, Clock::time_point deadline) {
    auto* next = static_cast<char*>(data);
    while (size > 0) {
        waitReady(input_.get(), POLLIN, deadline);
        const ssize_t count = read(input_.get(), next, size);
        if (count == 0) throw std::runtime_error("Соединение закрыто");
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            systemError("read");
        }
        next += count;
        size -= count;
    }
}

void Conn::writeBytes(const void* data, size_t size, Clock::time_point deadline) {
    const auto* next = static_cast<const char*>(data);
    while (size > 0) {
        waitReady(output_.get(), POLLOUT, deadline);
        const ssize_t count = write(output_.get(), next, size);
        if (count < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            systemError("write");
        }
        if (count == 0) throw std::runtime_error("Не удалось отправить сообщение");
        next += count;
        size -= count;
    }
}

QJsonObject Conn::Read() {
    receiveReady_.wait();
    const auto deadline = Clock::now() + connectionTimeout;
    quint32 length = 0;
    readBytes(&length, sizeof(length), deadline);
    length = qFromBigEndian(length);
    if (length == 0 || length > static_cast<quint32>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("Неверная длина сообщения");
    }
    QByteArray data(static_cast<int>(length), Qt::Uninitialized);
    readBytes(data.data(), static_cast<size_t>(data.size()), deadline);
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        throw std::runtime_error("Неверный формат сообщения");
    }
    receiveTurn_.post();
    return document.object();
}

void Conn::Write(const QJsonObject& message) {
    const QByteArray data = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (data.size() > std::numeric_limits<int>::max()) {
        throw std::runtime_error("Сообщение слишком длинное");
    }
    const quint32 length = qToBigEndian(static_cast<quint32>(data.size()));
    sendTurn_.wait();
    sendReady_.post();
    const auto deadline = Clock::now() + connectionTimeout;
    writeBytes(&length, sizeof(length), deadline);
    writeBytes(data.constData(), static_cast<size_t>(data.size()), deadline);
}
