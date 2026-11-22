#pragma once

#include <QElapsedTimer>
#include <QMainWindow>

#include <functional>

#include "host/HostObserver.h"

class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

namespace wolf {

// Окно волка. Методы HostObserver приходят из потоков сервера, поэтому каждый
// перекладывает работу в поток окна через QMetaObject::invokeMethod.
class HostWindow : public QMainWindow, public HostObserver {
    Q_OBJECT

public:
    HostWindow(const QString& connType, qint64 hostPid, QWidget* parent = nullptr);

    std::function<void(int)> onWolfNumber;
    std::function<void()> onAddKid;

    void onStatus(const std::string& text) override;
    void onKids(const std::vector<KidInfo>& kids) override;
    void onTurnStarted(int round, std::chrono::milliseconds window) override;
    void onRound(const game::RoundResult& result, WolfSource source) override;
    void onGameOver(int rounds) override;
    void onTerminateRequested() override;

private:
    void appendStatus(const QString& text);
    void showKids(const std::vector<KidInfo>& kids);
    void showRound(const game::RoundResult& result, WolfSource source);
    void startTurn(int round, qint64 windowMs);
    void updateCountdown();
    void throwClicked();

    QLabel* stateLabel_ = nullptr;
    QLabel* kidsLabel_ = nullptr;
    QSpinBox* wolfNumber_ = nullptr;
    QPushButton* throwButton_ = nullptr;
    QPushButton* addKidButton_ = nullptr;
    QProgressBar* turnBar_ = nullptr;
    QLabel* turnLabel_ = nullptr;
    QLabel* statsLabel_ = nullptr;
    QTableWidget* kidsTable_ = nullptr;
    QPlainTextEdit* history_ = nullptr;
    QPlainTextEdit* statusLog_ = nullptr;

    QTimer* countdown_ = nullptr;
    QElapsedTimer turnClock_;
    qint64 turnMs_ = 0;
    int turnRound_ = 0;
    bool numberSubmitted_ = false;
};

}
