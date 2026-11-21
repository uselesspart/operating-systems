#pragma once

#include "connection/conn.hpp"

#include <QObject>
#include <QString>

class HostWorker : public QObject {
    Q_OBJECT

public:
    explicit HostWorker(Conn* connection);

public slots:
    void requestGoat(int round, int maximum);
    void sendResult(int round, bool alive, bool game_over);

signals:
    void clientConnected();
    void goatReceived(int round, int value);
    void resultSent(int round, bool alive, bool game_over);
    void failed(const QString& message);

private:
    Conn* connection_;
    bool client_connected_;
};
