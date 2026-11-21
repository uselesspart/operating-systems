#pragma once

#include "../game_engine.hpp"
#include "connection/conn.hpp"

#include <QMainWindow>

class HostWorker;
class QString;
class QLabel;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QThread;
class QTimer;

class HostWindow : public QMainWindow {
public:
    explicit HostWindow(Conn* connection, pid_t client_pid);
    ~HostWindow() override;

private:
    void prepareRound();
    void startRound(int wolf_number);
    void handleGoat(int round, int goat_number);
    void handleResult(int round, bool alive, bool game_over);
    void checkChildProcess();
    void appendLog(const QString& message);

    Conn* connection_;
    HostWorker* worker_;
    QThread* worker_thread_;
    QTimer* child_watch_timer_;
    QTimer* countdown_timer_;
    QLabel* connection_label_;
    QLabel* status_label_;
    QLabel* round_label_;
    QSpinBox* wolf_input_;
    QPushButton* throw_button_;
    QTextEdit* log_view_;

    GameEngine game_;
    pid_t client_pid_;
    int current_round_;
    int current_wolf_number_;
    int countdown_seconds_;
    bool round_started_;
    bool game_over_;
    bool client_reaped_;
};
