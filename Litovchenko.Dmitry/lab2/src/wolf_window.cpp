#include "wolf_window.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>
#include <random>

#include "conn.h"
#include "wolf_session.h"

SessionThread::SessionThread(Shared* shared, pid_t goat, int id, int maxRounds, QObject* parent)
    : QThread(parent), shared_(shared), goat_(goat), id_(id), maxRounds_(maxRounds) {}

void SessionThread::submitWolfNumber(int number) {
    wolfNumber_ = number;
    input_.release();
}

void SessionThread::requestStop() {
    stop_ = true;
    input_.release();
}

void SessionThread::run() {
    std::mt19937 rng(std::random_device{}());
    WolfSession::Callbacks cb;
    cb.status = [this](const std::string& text) { emit status(QString::fromStdString(text)); };
    cb.roundDone = [this](const RoundResult& r) { emit roundDone(r); };
    cb.wolfNumber = [this, &rng](int round) {
        input_.tryAcquire(input_.available());
        emit waitingForWolf(round);
        if (input_.tryAcquire(1, kWolfInputMs) && !stop_) return WolfChoice{wolfNumber_, true};
        return WolfChoice{std::uniform_int_distribution<int>(1, 100)(rng), false};
    };
    WolfSession session(shared_, goat_, id_, cb);
    exitCode_ = session.run(maxRounds_, stop_);
}

WolfWindow::WolfWindow(SessionThread* session, bool quitWhenDone)
    : session_(session), quitWhenDone_(quitWhenDone) {
    setWindowTitle(QString("Волк и козлёнок — связь %1").arg(Conn::Type()));
    resize(760, 560);

    number_ = new QSpinBox;
    number_->setRange(1, 100);
    throwButton_ = new QPushButton("Бросить");
    countdown_ = new QProgressBar;
    countdown_->setRange(0, kWolfInputMs);
    countdown_->setFormat("до случайного броска");
    auto* input = new QHBoxLayout;
    input->addWidget(new QLabel("Число волка:"));
    input->addWidget(number_);
    input->addWidget(throwButton_);
    input->addWidget(countdown_, 1);

    rounds_ = new QTableWidget(0, 7);
    rounds_->setHorizontalHeaderLabels(
        {"Ход", "Волк", "Козлёнок", "Было", "Стало", "Спрятался / попался", "Мёртвых"});
    rounds_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rounds_->verticalHeader()->hide();
    rounds_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    totals_ = new QLabel;
    log_ = new QPlainTextEdit;
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(500);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(input);
    layout->addWidget(rounds_, 3);
    layout->addWidget(totals_);
    layout->addWidget(log_, 1);

    throwButton_->setEnabled(false);
    number_->setEnabled(false);
    tick_.setInterval(50);

    connect(throwButton_, &QPushButton::clicked, this, &WolfWindow::submit);
    connect(number_, &QSpinBox::editingFinished, this, [this] {
        if (throwButton_->isEnabled()) submit();
    });
    connect(&tick_, &QTimer::timeout, this, [this] {
        countdown_->setValue(kWolfInputMs - static_cast<int>(std::min<qint64>(elapsed_.elapsed(), kWolfInputMs)));
    });
    connect(session_, &SessionThread::status, this, &WolfWindow::addStatus);
    connect(session_, &SessionThread::waitingForWolf, this, &WolfWindow::startCountdown);
    connect(session_, &SessionThread::roundDone, this, &WolfWindow::showRound);
    connect(session_, &QThread::finished, this, &WolfWindow::sessionFinished);
    showRound(RoundResult{});
}

void WolfWindow::addStatus(const QString& text) {
    log_->appendPlainText(QDateTime::currentDateTime().toString("hh:mm:ss  ") + text);
}

void WolfWindow::startCountdown(int round) {
    countdown_->setFormat(QString("ход %1: %v мс до случайного броска").arg(round));
    countdown_->setValue(kWolfInputMs);
    elapsed_.start();
    tick_.start();
    number_->setEnabled(true);
    throwButton_->setEnabled(true);
}

void WolfWindow::submit() {
    tick_.stop();
    number_->setEnabled(false);
    throwButton_->setEnabled(false);
    session_->submitWolfNumber(number_->value());
}

void WolfWindow::showRound(const RoundResult& r) {
    if (r.round > 0) {
        tick_.stop();
        number_->setEnabled(false);
        throwButton_->setEnabled(false);
        hidden_ += r.hidden;
        caught_ += r.caught;
        revived_ += r.revived;

        auto state = [](bool alive) { return alive ? QString("жив") : QString("мёртв"); };
        QString outcome = r.hidden ? "спрятался" : r.caught ? "попался" : r.revived ? "воскрес" : "—";
        QStringList cells = {QString::number(r.round),
                             QString("%1 (%2)").arg(r.wolf).arg(r.wolfManual ? "вручную" : "случайно"),
                             QString::number(r.numbers.at(0)),
                             state(r.wasAlive.at(0)),
                             state(r.alive.at(0)),
                             outcome,
                             QString::number(r.dead)};
        int row = rounds_->rowCount();
        rounds_->insertRow(row);
        for (int col = 0; col < cells.size(); ++col) {
            auto* item = new QTableWidgetItem(cells[col]);
            item->setTextAlignment(Qt::AlignCenter);
            rounds_->setItem(row, col, item);
        }
        rounds_->scrollToBottom();
    }
    totals_->setText(QString("Всего — спрятался: %1, попался: %2, воскрес: %3")
                         .arg(hidden_)
                         .arg(caught_)
                         .arg(revived_));
}

void WolfWindow::sessionFinished() {
    tick_.stop();
    number_->setEnabled(false);
    throwButton_->setEnabled(false);
    countdown_->setFormat("игра завершена");
    countdown_->setValue(0);
    if (quitWhenDone_) QApplication::exit(session_->exitCode());
}

void WolfWindow::closeEvent(QCloseEvent* event) {
    session_->requestStop();
    session_->wait();
    event->accept();
}

int runGui(int argc, char* argv[], Shared* shared, pid_t goat, int id, int maxRounds) {
    QApplication app(argc, argv);
    qRegisterMetaType<RoundResult>();
    SessionThread session(shared, goat, id, maxRounds);
    WolfWindow window(&session, maxRounds > 0);
    window.show();
    session.start();
    int code = app.exec();
    session.requestStop();
    session.wait();
    return maxRounds > 0 ? code : session.exitCode();
}
