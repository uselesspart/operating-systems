#pragma once
#include <QMutex>
#include <QString>
#include <QThread>
#include <QWaitCondition>
#include <atomic>

class Conn;

class GameThread : public QThread {
    Q_OBJECT
public:
    explicit GameThread(Conn* conn, QObject* parent = nullptr);

    void RequestStop();
    void SubmitWolfNumber(int n);

signals:
    void Status(const QString& text);
    void WolfTurn(int round);

    void RoundFinished(int round, int wolf, int kid, int outcome);
    void GameOver(const QString& reason);

protected:
    void run() override;

private:
    int WaitWolfNumber(int round);

    Conn* conn_;
    std::atomic<bool> stop_{false};
    QMutex mutex_;
    QWaitCondition cond_;
    bool waiting_ = false;
    int wolfValue_ = 0;
};
