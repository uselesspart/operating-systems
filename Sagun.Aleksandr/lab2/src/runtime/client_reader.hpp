#pragma once

#include <QString>
#include <QThread>

class Connection;

// Один экземпляр на клиентское соединение хоста.
class ClientReader final : public QThread {
    Q_OBJECT

public:
    explicit ClientReader(Connection& connection);
    ~ClientReader() override;

signals:
    void received(const QString& text);
    void disconnected(const QString& reason);

protected:
    void run() override;

private:
    Connection& connection_;
};
