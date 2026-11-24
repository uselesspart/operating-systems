#pragma once

#include "ipc/connection.hpp"

#include <QElapsedTimer>
#include <QString>
#include <memory>
#include <sys/types.h>
#include <vector>

struct Session {
    explicit Session(int number)
        : name(QStringLiteral("Клиент %1").arg(number)),
          channel(openConnection()) {}

    QString name;
    std::unique_ptr<Connection> channel;
    pid_t pid = -1;
    QElapsedTimer idle;
    bool online = true;
    bool greeted = false;
};

using Sessions = std::vector<std::unique_ptr<Session>>;

bool stillRunning(Session& session);
int runHost(int& argc, char** argv, Sessions& sessions);
int runClient(int& argc, char** argv, int self, int clientCount, Connection& channel);
