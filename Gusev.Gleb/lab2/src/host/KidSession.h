#pragma once

#include <sys/types.h>

#include <semaphore.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "conn/conn.h"
#include "ipc/Protocol.h"

namespace wolf {

// Поток хоста на одного козлёнка: очередь сообщений, канал и два локальных
// семафора. Семафор toKid будит потомка после записи, toHost — хоста после ответа.
class KidSession {
public:
    using ReplyHandler = std::function<void(int kidId, const proto::Message& reply)>;
    using FailureHandler = std::function<void(int kidId, const std::string& reason)>;

    KidSession(const ConnId& id, pid_t pid, std::unique_ptr<Conn> conn, sem_t* toKid,
               sem_t* toHost, ReplyHandler onReply, FailureHandler onFailure);
    ~KidSession();

    KidSession(const KidSession&) = delete;
    KidSession& operator=(const KidSession&) = delete;

    void start();
    void send(const proto::Message& message, bool expectReply);
    void stop();
    void join();

    int id() const { return id_.clientId; }
    pid_t pid() const { return pid_; }
    bool finished() const { return finished_; }

private:
    struct Task {
        proto::Message message;
        bool expectReply = false;
    };

    void run();
    bool process(const Task& task, std::string& reason);

    ConnId id_;
    pid_t pid_;
    std::unique_ptr<Conn> conn_;
    sem_t* toKid_;
    sem_t* toHost_;
    ReplyHandler onReply_;
    FailureHandler onFailure_;

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    bool stopRequested_ = false;
    std::atomic<bool> finished_{false};
};

}
