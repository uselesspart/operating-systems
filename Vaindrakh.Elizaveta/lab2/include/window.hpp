#pragma once

#include "session.hpp"
#include <QElapsedTimer>
#include <QMainWindow>
#include <QTimer>
#include <thread>

class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTextEdit;

namespace lab {
class Window final : public QMainWindow {
public:
    explicit Window(Session& session);
    ~Window() override;
    bool failed() const { return failed_; }
private:
    void connectClient();
    void beginTurn();
    void submit(int wolf);
    void showRound(const Round& round);
    void showError(const QString& error);
    void addEvent(const QString& text);
    void joinWorker();
    Session& session_;
    std::thread worker_;
    QTimer timer_;
    QTimer signalTimer_;
    QElapsedTimer deadline_;
    QLabel* status_;
    QLabel* goat_;
    QLabel* countdown_;
    QSpinBox* number_;
    QPushButton* submit_;
    QTableWidget* table_;
    QTextEdit* events_;
    bool accepting_ = false;
    bool failed_ = false;
};
}
