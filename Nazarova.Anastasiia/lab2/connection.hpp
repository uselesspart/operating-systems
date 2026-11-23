#pragma once

#include "system.hpp"
#include <QJsonObject>
#include <vector>

class Conn {
public:
    Conn(pid_t host, pid_t client, bool create);
    void Accept();
    void Connected();
    QJsonObject Read();
    void Write(const QJsonObject& message);
private:
    void Prepare(pid_t host, bool create);
    void readBytes(void* data, size_t size, Clock::time_point deadline);
    void writeBytes(const void* data, size_t size, Clock::time_point deadline);
    Directory directory_;
    Semaphore sendTurn_;
    Semaphore sendReady_;
    Semaphore receiveTurn_;
    Semaphore receiveReady_;
    Fd input_;
    Fd output_;
    Fd listener_;
    std::vector<Fd> pending_;
};
