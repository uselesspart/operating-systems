#include "chat.hpp"

#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

Server::Server(EventHandler handler)
    : pid_(getpid()), directory_(hostDirectory(pid_), true),
      registration_(resourceName(pid_, 0, "join"), true, 1), event_(std::move(handler)) {
    event_({{"type", "members"}, {"members", QJsonArray{pid_}},
            {"text", QString("Хост запущен. PID: %1").arg(pid_)}});
    listener_ = std::thread(&Server::listen, this);
}

Server::~Server() { Stop(); }

void Server::Stop() {
    running_ = false;
    if (listener_.joinable()) listener_.join();
    for (const auto& item : sessions_) {
        if (item.second->worker.joinable()) item.second->worker.join();
    }
    sessions_.clear();
}

void Server::Send(pid_t recipient, const QString& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    route(chatMessage(pid_, recipient, text, ++serial_));
}

void Server::route(const QJsonObject& message) {
    const pid_t sender = message["sender"].toInt();
    validateMessage(message, sender);
    const pid_t recipient = message["recipient"].toInt();
    if (recipient != 0 && recipient != pid_
        && (sessions_.count(recipient) == 0 || !sessions_.at(recipient)->active)) {
        const QJsonObject error{{"type", "error"}, {"text", "Адресат уже отключился"}};
        if (sender == pid_) event_(error);
        else sessions_.at(sender)->outgoing.push_back(error);
        return;
    }
    if (recipient == 0 || recipient == pid_ || sender == pid_) event_(message);
    for (const auto& item : sessions_) {
        if (item.second->active && (recipient == 0 || recipient == item.first || sender == item.first)) {
            item.second->outgoing.push_back(message);
        }
    }
}

void Server::membership(const QString& text) {
    QJsonArray members{pid_};
    for (const auto& item : sessions_) {
        if (item.second->active) members.append(item.first);
    }
    const QJsonObject update{{"type", "members"}, {"members", members}, {"text", text}};
    event_(update);
    for (const auto& item : sessions_) {
        if (item.second->active) item.second->outgoing.push_back(update);
    }
}

QJsonObject Server::take(const std::shared_ptr<Session>& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    QJsonArray messages;
    for (const auto& message : session->outgoing) messages.append(message);
    session->outgoing.clear();
    return {{"type", "batch"}, {"events", messages}};
}

void Server::serve(const std::shared_ptr<Session>& session) {
    QString reason = QString("Клиент %1 отключился").arg(session->pid);
    try {
        Process peer(session->pid);
        Conn connection(pid_, session->pid, true);
        peer.send(SIGUSR2);
        connection.Accept();
        const auto hello = connection.Read();
        if (hello["type"] != "hello" || hello["transport"] != TYPE_CODE) {
            throw std::runtime_error("Неверный запрос подключения");
        }
        connection.Connected();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session->active = true;
            membership(QString("Клиент %1 подключился").arg(session->pid));
        }
        connection.Write(take(session));
        auto lastMessage = Clock::now();
        while (true) {
            const auto request = connection.Read();
            if (!running_ || request["type"] == "leave") {
                connection.Write({{"type", "closed"}, {"text", "Соединение закрыто"}});
                break;
            }
            if (peer.finished()) break;
            if (Clock::now() - lastMessage >= idleTimeout) {
                peer.send(SIGKILL);
                reason = QString("Клиент %1 отключён: нет сообщений больше минуты").arg(session->pid);
                break;
            }
            if (request["type"] == "chat") {
                validateMessage(request, session->pid);
                std::lock_guard<std::mutex> lock(mutex_);
                route(request);
                lastMessage = Clock::now();
            } else if (request["type"] != "poll") {
                throw std::runtime_error("Неизвестный запрос");
            }
            connection.Write(take(session));
        }
    } catch (const std::exception& error) {
        reason += ": " + QString::fromUtf8(error.what());
        if (running_) std::cerr << reason.toStdString() << '\n';
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        session->active = false;
        if (running_) membership(reason);
    }
    session->done = true;
}

void Server::reap() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second->done) {
            it->second->worker.join();
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void Server::listen() {
    const auto signalMask = signalSet(SIGUSR1);
    const timespec timeout{0, std::chrono::duration_cast<std::chrono::nanoseconds>(pollInterval).count()};
    try {
        while (running_) {
            siginfo_t info{};
            const int result = sigtimedwait(&signalMask, &info, &timeout);
            if (result < 0 && errno != EAGAIN && errno != EINTR) systemError("sigtimedwait");
            reap();
            if (result != SIGUSR1 || info.si_uid != getuid() || info.si_pid <= 1 || info.si_pid == pid_) continue;
            std::lock_guard<std::mutex> lock(mutex_);
            if (sessions_.count(info.si_pid) != 0) continue;
            auto session = std::make_shared<Session>(info.si_pid);
            sessions_.emplace(info.si_pid, session);
            try {
                session->worker = std::thread(&Server::serve, this, session);
            } catch (...) {
                sessions_.erase(info.si_pid);
                throw;
            }
        }
    } catch (const std::exception& error) {
        running_ = false;
        event_({{"type", "closed"}, {"text", QString::fromUtf8(error.what())}});
    }
}
