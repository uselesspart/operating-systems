#pragma once
#include <QMainWindow>
#include <sys/types.h>

class GameThread;
class Conn;
class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(Conn* conn, pid_t kidPid, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void OnStatus(const QString& text);
    void OnWolfTurn(int round);
    void OnRoundFinished(int round, int wolf, int kid, int outcome);
    void OnGameOver(const QString& reason);
    void OnSubmit();
    void OnTick();

private:
    void Append(const QString& text);
    void SetInputEnabled(bool on);

    GameThread* game_;
    QPlainTextEdit* log_;
    QLabel* statusLabel_;
    QLabel* roundLabel_;
    QLabel* timerLabel_;
    QSpinBox* spin_;
    QPushButton* button_;
    QTimer* timer_;
    int remainingMs_ = 0;
};
