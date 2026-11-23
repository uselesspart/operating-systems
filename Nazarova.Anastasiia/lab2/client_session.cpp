#include "chat.hpp"

#include <cerrno>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

Client::Client(pid_t host, EventHandler handler)
    : host_(host), event_(std::move(handler)), worker_(&Client::run, this) {}
Client::~Client() { Stop(); }

void Client::Send(pid_t recipient, const QString& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto message = chatMessage(getpid(), recipient, text, ++serial_);
    validateMessage(message, getpid());
    if (!running_) throw std::runtime_error("Клиент отключён");
    outgoing_.push_back(std::move(message));
    changed_.notify_one();
}

void Client::Stop() {
    running_ = false;
    changed_.notify_one();
    if (worker_.joinable()) worker_.join();
}

void Client::receive(const QJsonObject& batch) {
    if (batch["type"] == "closed") {
        running_ = false;
        event_(batch);
        return;
    }
    if (batch["type"] != "batch" || !batch["events"].isArray()) {
        throw std::runtime_error("Неверный ответ хоста");
    }
    for (const auto& event : batch["events"].toArray()) {
        if (!event.isObject()) throw std::runtime_error("Неверный ответ хоста");
        event_(event.toObject());
    }
}

void Client::run() {
    try {
        Process host(host_);
        Semaphore registration(resourceName(host_, 0, "join"), false, 1);
        registration.wait();
        try {
            host.send(SIGUSR1);
            const auto signalMask = signalSet(SIGUSR2);
            const auto deadline = Clock::now() + connectionTimeout;
            while (true) {
                const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - Clock::now());
                if (left.count() <= 0) throw std::runtime_error("Хост не ответил за 5 секунд");
                const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(left);
                const timespec timeout{seconds.count(), (left - seconds).count()};
                siginfo_t info{};
                const int result = sigtimedwait(&signalMask, &info, &timeout);
                if (result < 0) {
                    if (errno == EINTR) continue;
                    systemError("sigtimedwait");
                }
                if (info.si_pid == host_ && info.si_uid == getuid()) break;
            }
        } catch (...) {
            registration.post();
            throw;
        }
        registration.post();
        Conn connection(host_, getpid(), false);
        connection.Write({{"type", "hello"}, {"transport", TYPE_CODE}});
        receive(connection.Read());
        while (running_) {
            QJsonObject request{{"type", "poll"}};
            {
                std::unique_lock<std::mutex> lock(mutex_);
                changed_.wait_for(lock, pollInterval, [this] { return !outgoing_.empty() || !running_; });
                if (!running_) break;
                if (!outgoing_.empty()) {
                    request = std::move(outgoing_.front());
                    outgoing_.pop_front();
                }
            }
            if (host.finished()) throw std::runtime_error("Хост завершил работу");
            connection.Write(request);
            receive(connection.Read());
        }
        if (!host.finished()) {
            connection.Write({{"type", "leave"}});
        }
    } catch (const std::exception& error) {
        if (running_) {
            std::cerr << error.what() << '\n';
            event_({{"type", "closed"}, {"text", QString::fromUtf8(error.what())}});
        }
    }
    running_ = false;
}
