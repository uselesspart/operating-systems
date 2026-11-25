#pragma once

#include <sys/types.h>

#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QWidget>
#include <atomic>

#include "common.h"
#include "game.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

Q_DECLARE_METATYPE(RoundResult)

class SessionThread : public QThread {
    Q_OBJECT
public:
    SessionThread(Shared* shared, pid_t goat, int id, int maxRounds, QObject* parent = nullptr);

    void submitWolfNumber(int number);
    void requestStop();
    int exitCode() const { return exitCode_; }

signals:
    void status(const QString& text);
    void waitingForWolf(int round);
    void roundDone(const RoundResult& result);

protected:
    void run() override;

private:
    Shared* shared_;
    pid_t goat_;
    int id_;
    int maxRounds_;
    int exitCode_ = 0;
    QSemaphore input_;
    std::atomic<int> wolfNumber_{0};
    std::atomic<bool> stop_{false};
};

class WolfWindow : public QWidget {
    Q_OBJECT
public:
    WolfWindow(SessionThread* session, bool quitWhenDone);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void addStatus(const QString& text);
    void startCountdown(int round);
    void submit();
    void showRound(const RoundResult& r);
    void sessionFinished();

    SessionThread* session_;
    bool quitWhenDone_;
    QSpinBox* number_;
    QPushButton* throwButton_;
    QProgressBar* countdown_;
    QTableWidget* rounds_;
    QLabel* totals_;
    QPlainTextEdit* log_;
    QTimer tick_;
    QElapsedTimer elapsed_;
    int hidden_ = 0;
    int caught_ = 0;
    int revived_ = 0;
};

int runGui(int argc, char* argv[], Shared* shared, pid_t goat, int id, int maxRounds);
