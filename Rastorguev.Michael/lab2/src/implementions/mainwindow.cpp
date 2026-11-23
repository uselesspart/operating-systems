#include "mainwindow.h"

#include <QCloseEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

#include "gamethread.h"
#include "protocol.h"
#include "rules.h"

MainWindow::MainWindow(Conn* conn, pid_t kidPid, QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Волк и козлята — хост (волк)");
    resize(640, 480);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);

    statusLabel_ = new QLabel("Запуск...", central);
    statusLabel_->setStyleSheet("font-weight: bold;");
    roundLabel_ = new QLabel("Раунд: 0", central);
    layout->addWidget(statusLabel_);
    layout->addWidget(roundLabel_);

    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel("Число волка (1–100):", central));
    spin_ = new QSpinBox(central);
    spin_->setRange(1, 100);
    spin_->setValue(50);
    row->addWidget(spin_);
    button_ = new QPushButton("Бросить", central);
    row->addWidget(button_);
    timerLabel_ = new QLabel(central);
    row->addWidget(timerLabel_, 1);
    layout->addLayout(row);

    log_ = new QPlainTextEdit(central);
    log_->setReadOnly(true);
    layout->addWidget(log_, 1);
    setCentralWidget(central);

    SetInputEnabled(false);
    timer_ = new QTimer(this);
    timer_->setInterval(100);
    connect(timer_, &QTimer::timeout, this, &MainWindow::OnTick);
    connect(button_, &QPushButton::clicked, this, &MainWindow::OnSubmit);

    game_ = new GameThread(conn, this);
    connect(game_, &GameThread::Status, this, &MainWindow::OnStatus);
    connect(game_, &GameThread::WolfTurn, this, &MainWindow::OnWolfTurn);
    connect(game_, &GameThread::RoundFinished, this, &MainWindow::OnRoundFinished);
    connect(game_, &GameThread::GameOver, this, &MainWindow::OnGameOver);

    Append(QString("Хост запущен, козлёнок — процесс pid %1").arg(kidPid));
    game_->start();
}

void MainWindow::Append(const QString& text) {
    log_->appendPlainText(QDateTime::currentDateTime().toString("[HH:mm:ss] ") + text);
}

void MainWindow::SetInputEnabled(bool on) {
    spin_->setEnabled(on);
    button_->setEnabled(on);
    if (!on) timerLabel_->clear();
}

void MainWindow::OnStatus(const QString& text) {
    statusLabel_->setText(text);
    Append(text);
}

void MainWindow::OnWolfTurn(int round) {
    Append(QString("Раунд %1: ход волка — введите число за 3 сек").arg(round));
    statusLabel_->setText(QString("Раунд %1: ваш ход, волк!").arg(round));
    remainingMs_ = proto::kWolfInputTimeoutMs;
    SetInputEnabled(true);
    spin_->setFocus();
    spin_->selectAll();
    OnTick();
    timer_->start();
}

void MainWindow::OnTick() {
    timerLabel_->setText(QString("Осталось: %1 с").arg(qMax(remainingMs_, 0) / 1000.0, 0, 'f', 1));
    remainingMs_ -= timer_->interval();
    if (remainingMs_ < -timer_->interval()) timer_->stop();
}

void MainWindow::OnSubmit() {
    game_->SubmitWolfNumber(spin_->value());
    Append(QString("Волк ввёл число: %1").arg(spin_->value()));
    timer_->stop();
    SetInputEnabled(false);
}

void MainWindow::OnRoundFinished(int round, int wolf, int kid, int outcome) {
    timer_->stop();
    SetInputEnabled(false);
    const auto result = static_cast<rules::Outcome>(outcome);
    const bool alive = rules::AliveAfter(result);
    const int hidden = result == rules::Outcome::kHidden ? 1 : 0;
    const int caught = result == rules::Outcome::kCaught ? 1 : 0;
    const int dead = alive ? 0 : 1;
    Append(QString("Раунд %1: волк — %2, козлёнок — %3 | спрятались: %4, попались: %5, мертвы: %6 "
                   "(козлёнок %7)")
               .arg(round).arg(wolf).arg(kid).arg(hidden).arg(caught).arg(dead)
               .arg(QString::fromUtf8(rules::OutcomeName(result))));
    roundLabel_->setText(QString("Раунд: %1 | козлёнок: %2").arg(round).arg(alive ? "жив" : "мёртв"));
    statusLabel_->setText(QString("Раунд %1 завершён").arg(round));
}

void MainWindow::OnGameOver(const QString& reason) {
    timer_->stop();
    SetInputEnabled(false);
    statusLabel_->setText("Игра окончена");
    Append("Игра окончена: " + reason);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    game_->RequestStop();
    if (!game_->wait(8000)) {
        game_->terminate();
        game_->wait();
    }
    e->accept();
}
