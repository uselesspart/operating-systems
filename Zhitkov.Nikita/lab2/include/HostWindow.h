#pragma once
#include <QMainWindow>
#include <QTextEdit>
#include <QLabel>
#include <atomic>
#include <future>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "Conn.h"
#include "SharedSems.h"
#include "Scenario.h"

class HostWindow : public QMainWindow {
    Q_OBJECT
public:
    HostWindow(Conn* conn, SharedSems* sems, QWidget* parent = nullptr);
    ~HostWindow() override;

signals:
    void logAppend(const QString& msg);
    void roundFinished(int round, int wolf, int kid,
                       const QString& status, bool alive_after,
                       int hid, int caught, int resurrected);
    void needWolfInput();

private slots:
    void onLogAppend(const QString& msg);
    void onRoundFinished(int round, int wolf, int kid,
                         const QString& status, bool alive_after,
                         int hid, int caught, int resurrected);
    void onNeedWolfInput();

private:
    void gameLoop();
    int  getWolfNum();

    Conn*        conn_;
    SharedSems*  sems_;
    Scenario     scenario_;
    QTextEdit*   log_;
    QLabel*      header_;
    std::thread  thread_;
    std::atomic<bool> running_{true};

    // Синхронизация gameLoop-GUI при вводе числа волка
    std::mutex              input_mtx_;
    std::condition_variable input_cv_;
    int                     input_value_ = 0;
    bool                    input_ready_ = false;
};