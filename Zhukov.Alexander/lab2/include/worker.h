#pragma once

#include "session.h"
#include <QThread>
#include <deque>
#include <mutex>

class Worker : public QThread {
    Q_OBJECT

public:
    Worker(std::unique_ptr<Conn> conn, pid_t host_pid, bool host);
    ~Worker() override { stop(); wait(); }
    void stop() { stopped.store(true); }
    bool enqueue(int recipient, const QString& text);

signals:
    void status_changed(const QString& text);
    void connected_changed(bool connected);
    void history_changed(const History& messages);
    void done(int code);

protected:
    void run() override;

private:
    struct Outgoing {
        Message message;
        Deadline deadline;
        bool forwarded = false;
    };
    std::unique_ptr<Conn> conn;
    pid_t host_pid;
    bool host;
    std::atomic_bool stopped{false};
    std::atomic_bool ready{false};
    std::mutex mutex;
    std::deque<Outgoing> outgoing;
};
