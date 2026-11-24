#pragma once

#include "host/host_controller.hpp"

#include <QMainWindow>
#include <QString>

class QLabel;
class QSpinBox;
class QPushButton;
class QProgressBar;
class QTableWidget;
class QPlainTextEdit;
class QTimer;

namespace lab2::gui {

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(host::HostController& controller, const QString& transport, pid_t hostPid,
                        QWidget* parent = nullptr);

    void applyEvent(const host::HostEvent& event);
    void refreshInput();
    void stopGame();

private:
    void appendLog(const QString& message);
    void updateClientCount();
    int findClientRow(game::ClientId clientId) const;
    void displayRound(const game::RoundResult& result);

    host::HostController& controller_;
    QLabel* status_{};
    QLabel* clientsCount_{};
    QLabel* roundLabel_{};
    QLabel* statistics_{};
    QLabel* countdownLabel_{};
    QSpinBox* number_{};
    QPushButton* submit_{};
    QPushButton* stop_{};
    QProgressBar* countdown_{};
    QTableWidget* clients_{};
    QTableWidget* history_{};
    QPlainTextEdit* log_{};
    QTimer* timer_{};
    std::uint64_t currentRound_{};
    bool stopping_{};
    bool finished_{};
    bool submitted_{};
};

} // namespace lab2::gui
