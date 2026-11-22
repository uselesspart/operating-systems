#include "host/KidSession.h"

#include <semaphore.h>

#include <utility>

#include "ipc/LocalSemaphore.h"
#include "ipc/Timeouts.h"

namespace wolf {

KidSession::KidSession(const ConnId& id, pid_t pid, std::unique_ptr<Conn> conn, sem_t* toKid,
                       sem_t* toHost, ReplyHandler onReply, FailureHandler onFailure)
    : id_(id),
      pid_(pid),
      conn_(std::move(conn)),
      toKid_(toKid),
      toHost_(toHost),
      onReply_(std::move(onReply)),
      onFailure_(std::move(onFailure)) {}

KidSession::~KidSession() {
    stop();
    join();
}

void KidSession::start() { thread_ = std::thread(&KidSession::run, this); }

void KidSession::send(const proto::Message& message, bool expectReply) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({message, expectReply});
    }
    cv_.notify_one();
}

void KidSession::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopRequested_ = true;
    }
    cv_.notify_one();
}

void KidSession::join() {
    if (thread_.joinable()) thread_.join();
}

bool KidSession::process(const Task& task, std::string& reason) {
    if (!conn_->Write(&task.message, sizeof task.message)) {
        reason = "не удалось отправить " + std::string(proto::typeName(task.message.type));
        return false;
    }
    sem::post(toKid_);
    if (!task.expectReply) return true;

    if (!sem::wait(toHost_, kIpcTimeout)) {
        reason = "не ответил за 5 с (семафор)";
        return false;
    }

    proto::Message reply;
    if (!conn_->Read(&reply, sizeof reply)) {
        reason = "канал закрыт при чтении ответа";
        return false;
    }
    if (!proto::isValid(reply)) {
        reason = "получено некорректное сообщение";
        return false;
    }
    onReply_(id_.clientId, reply);
    return true;
}

void KidSession::run() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopRequested_ || !queue_.empty(); });
            if (queue_.empty()) break;
            task = queue_.front();
            queue_.pop_front();
        }

        std::string reason;
        if (!process(task, reason)) {
            finished_ = true;
            onFailure_(id_.clientId, reason);
            return;
        }
        if (task.message.type == proto::Type::GameOver || task.message.type == proto::Type::Bye) {
            break;
        }
    }
    finished_ = true;
}

}
