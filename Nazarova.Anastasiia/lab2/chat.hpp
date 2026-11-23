#pragma once

#include "connection.hpp"
#include <QJsonArray>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

using EventHandler = std::function<void(const QJsonObject&)>;
QJsonObject chatMessage(pid_t sender, pid_t recipient, const QString& text, quint64 serial);
void validateMessage(const QJsonObject& message, pid_t sender);
bool earlierMessage(const QJsonObject& left, const QJsonObject& right);

class Endpoint {
public:
    virtual ~Endpoint() = default;
    virtual void Send(pid_t recipient, const QString& text) = 0;
    virtual void Stop() = 0;
};

class Server final : public Endpoint {
public:
    explicit Server(EventHandler handler);
    ~Server() override;
    void Send(pid_t recipient, const QString& text) override;
    void Stop() override;
private:
    struct Session {
        explicit Session(pid_t value) : pid(value) {}
        pid_t pid;
        bool active = false;
        std::atomic<bool> done{false};
        std::deque<QJsonObject> outgoing;
        std::thread worker;
    };
    void listen();
    void serve(const std::shared_ptr<Session>& session);
    void route(const QJsonObject& message);
    void membership(const QString& text);
    QJsonObject take(const std::shared_ptr<Session>& session);
    void reap();
    pid_t pid_;
    Directory directory_;
    Semaphore registration_;
    EventHandler event_;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::map<pid_t, std::shared_ptr<Session>> sessions_;
    quint64 serial_ = 0;
    std::thread listener_;
};

class Client final : public Endpoint {
public:
    Client(pid_t host, EventHandler handler);
    ~Client() override;
    void Send(pid_t recipient, const QString& text) override;
    void Stop() override;
private:
    void run();
    void receive(const QJsonObject& batch);
    pid_t host_;
    EventHandler event_;
    std::atomic<bool> running_{true};
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<QJsonObject> outgoing_;
    quint64 serial_ = 0;
    std::thread worker_;
};
