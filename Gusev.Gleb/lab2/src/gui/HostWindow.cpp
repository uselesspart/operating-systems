#include "gui/HostWindow.h"

#include <QApplication>
#include <QDateTime>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace wolf {

namespace {

QString outcomeText(proto::Outcome o) { return QString::fromUtf8(proto::outcomeName(o)); }

QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}

HostWindow::HostWindow(const QString& connType, qint64 hostPid, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(
        QString("Волк и семеро козлят — волк (host_%1, pid %2)").arg(connType).arg(hostPid));
    resize(900, 740);

    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);

    auto* header = new QLabel(
        QString("<b>Волк</b> &nbsp;·&nbsp; PID <b>%1</b> &nbsp;·&nbsp; канал <b>%2</b><br>"
                "Козлята — дочерние процессы волка, созданные через fork")
            .arg(hostPid)
            .arg(connType),
        central);
    header->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(header);

    auto* kidRow = new QHBoxLayout();
    kidsLabel_ = new QLabel("Козлят: 0", central);
    kidsLabel_->setObjectName("kidsLabel");
    kidRow->addWidget(kidsLabel_);
    addKidButton_ = new QPushButton("Добавить козлёнка", central);
    addKidButton_->setObjectName("addKidButton");
    kidRow->addWidget(addKidButton_);
    kidRow->addStretch();
    root->addLayout(kidRow);

    stateLabel_ = new QLabel("Козлят нет — нажмите «Добавить козлёнка»", central);
    stateLabel_->setObjectName("stateLabel");
    root->addWidget(stateLabel_);

    auto* turnBox = new QGroupBox("Ход волка", central);
    auto* turnLayout = new QVBoxLayout(turnBox);
    auto* inputRow = new QHBoxLayout();
    inputRow->addWidget(new QLabel("Число 1..100:", turnBox));
    wolfNumber_ = new QSpinBox(turnBox);
    wolfNumber_->setObjectName("wolfNumber");
    wolfNumber_->setRange(1, 100);
    wolfNumber_->setValue(50);
    inputRow->addWidget(wolfNumber_);
    throwButton_ = new QPushButton("Бросить", turnBox);
    throwButton_->setObjectName("throwButton");
    inputRow->addWidget(throwButton_);
    inputRow->addStretch();
    turnLayout->addLayout(inputRow);
    turnBar_ = new QProgressBar(turnBox);
    turnBar_->setRange(0, 1000);
    turnBar_->setValue(0);
    turnBar_->setTextVisible(false);
    turnLayout->addWidget(turnBar_);
    turnLabel_ = new QLabel("Если за 3 с число не введено, волк бросает случайно", turnBox);
    turnLabel_->setObjectName("turnLabel");
    turnLayout->addWidget(turnLabel_);
    root->addWidget(turnBox);

    statsLabel_ = new QLabel("Раундов ещё не было", central);
    statsLabel_->setObjectName("statsLabel");
    root->addWidget(statsLabel_);

    auto* splitter = new QSplitter(Qt::Vertical, central);
    kidsTable_ = new QTableWidget(0, 5, splitter);
    kidsTable_->setObjectName("kidsTable");
    kidsTable_->setHorizontalHeaderLabels({"Козлёнок", "PID", "Состояние", "Бросок", "Итог раунда"});
    kidsTable_->horizontalHeader()->setStretchLastSection(true);
    kidsTable_->verticalHeader()->setVisible(false);

    auto* historyBox = new QGroupBox("Ход игры", splitter);
    auto* historyLayout = new QVBoxLayout(historyBox);
    history_ = new QPlainTextEdit(historyBox);
    history_->setObjectName("historyView");
    history_->setReadOnly(true);
    history_->setPlaceholderText("Число волка, броски козлят и итоги каждого раунда");
    historyLayout->addWidget(history_);

    auto* statusBox = new QGroupBox("События", splitter);
    auto* statusLayout = new QVBoxLayout(statusBox);
    statusLog_ = new QPlainTextEdit(statusBox);
    statusLog_->setObjectName("statusView");
    statusLog_->setReadOnly(true);
    statusLog_->setPlaceholderText("Рождение, отключение, начало и конец игры");
    statusLayout->addWidget(statusLog_);
    splitter->setSizes({200, 300, 160});
    root->addWidget(splitter, 1);

    setCentralWidget(central);

    countdown_ = new QTimer(this);
    countdown_->setInterval(50);
    connect(countdown_, &QTimer::timeout, this, &HostWindow::updateCountdown);
    connect(throwButton_, &QPushButton::clicked, this, &HostWindow::throwClicked);
    connect(addKidButton_, &QPushButton::clicked, this, [this] {
        if (onAddKid) onAddKid();
    });
    connect(wolfNumber_, &QSpinBox::editingFinished, this, [this] {
        if (wolfNumber_->hasFocus()) throwClicked();
    });

    appendStatus(QString("Волк запущен (pid %1, канал %2)").arg(hostPid).arg(connType));
}

void HostWindow::onStatus(const std::string& text) {
    const QString t = QString::fromStdString(text);
    QMetaObject::invokeMethod(this, [this, t] { appendStatus(t); }, Qt::QueuedConnection);
}

void HostWindow::onKids(const std::vector<KidInfo>& kids) {
    QMetaObject::invokeMethod(this, [this, kids] { showKids(kids); }, Qt::QueuedConnection);
}

void HostWindow::onTurnStarted(int round, std::chrono::milliseconds window) {
    const qint64 ms = window.count();
    QMetaObject::invokeMethod(this, [this, round, ms] { startTurn(round, ms); },
                              Qt::QueuedConnection);
}

void HostWindow::onRound(const game::RoundResult& result, WolfSource source) {
    QMetaObject::invokeMethod(this, [this, result, source] { showRound(result, source); },
                              Qt::QueuedConnection);
}

void HostWindow::onGameOver(int rounds) {
    QMetaObject::invokeMethod(
        this,
        [this, rounds] {
            countdown_->stop();
            turnBar_->setValue(0);
            stateLabel_->setText(
                QString("Игра окончена на раунде %1 — волк победил. Добавьте козлят для новой игры")
                    .arg(rounds));
            history_->appendPlainText(QString("=== Игра окончена на раунде %1 ===").arg(rounds));
        },
        Qt::QueuedConnection);
}

void HostWindow::onTerminateRequested() {
    QMetaObject::invokeMethod(qApp, [] { QApplication::quit(); }, Qt::QueuedConnection);
}

void HostWindow::appendStatus(const QString& text) {
    statusLog_->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss") + "  " + text);
}

void HostWindow::showKids(const std::vector<KidInfo>& kids) {
    kidsTable_->setRowCount(static_cast<int>(kids.size()));
    int row = 0;
    int alive = 0;
    for (const auto& k : kids) {
        if (k.alive) ++alive;
        kidsTable_->setItem(row, 0, cell(QString("#%1").arg(k.id)));
        kidsTable_->setItem(row, 1, cell(QString::number(k.pid)));
        kidsTable_->setItem(row, 2, cell(k.alive ? "жив" : "мёртв"));
        kidsTable_->setItem(row, 3, cell(k.lastNumber ? QString::number(k.lastNumber) : "—"));
        kidsTable_->setItem(row, 4, cell(outcomeText(k.lastOutcome)));
        ++row;
    }
    kidsLabel_->setText(QString("Козлят: %1, живых: %2").arg(kids.size()).arg(alive));
    if (kids.empty() && !countdown_->isActive()) {
        stateLabel_->setText("Козлят нет — нажмите «Добавить козлёнка»");
    }
}

void HostWindow::startTurn(int round, qint64 windowMs) {
    turnRound_ = round;
    turnMs_ = windowMs;
    numberSubmitted_ = false;
    turnClock_.start();
    countdown_->start();
    throwButton_->setEnabled(true);
    stateLabel_->setText(QString("Раунд %1: ход волка").arg(round));
    updateCountdown();
}

void HostWindow::updateCountdown() {
    const qint64 left = std::max<qint64>(0, turnMs_ - turnClock_.elapsed());
    turnBar_->setValue(turnMs_ > 0 ? static_cast<int>(1000 * left / turnMs_) : 0);
    if (numberSubmitted_) return;
    if (left == 0) {
        countdown_->stop();
        turnLabel_->setText("Время вышло — волк бросает случайное число");
        return;
    }
    turnLabel_->setText(
        QString("Введите число и нажмите «Бросить»: осталось %1 с, потом бросок случайный")
            .arg(left / 1000.0, 0, 'f', 1));
}

void HostWindow::throwClicked() {
    const int n = wolfNumber_->value();
    numberSubmitted_ = true;
    turnLabel_->setText(QString("Принято: волк бросит %1").arg(n));
    if (onWolfNumber) onWolfNumber(n);
}

void HostWindow::showRound(const game::RoundResult& r, WolfSource source) {
    countdown_->stop();
    turnBar_->setValue(0);
    stateLabel_->setText(QString("Раунд %1 сыгран, козлят: %2").arg(r.round).arg(r.kids));
    statsLabel_->setText(
        QString("Раунд %1 · волк %2 (%3) · спрятались %4 · попались %5 · воскресли %6 · мертвы %7")
            .arg(r.round)
            .arg(r.wolf)
            .arg(QString::fromUtf8(wolfSourceName(source)))
            .arg(r.hidden)
            .arg(r.caught)
            .arg(r.resurrected)
            .arg(r.dead));

    QString line = QString("Раунд %1: волк %2 (%6), козлят %3 (укрыться: разница ≤ %4, воскреснуть: ≤ %5)")
                       .arg(r.round)
                       .arg(r.wolf)
                       .arg(r.kids)
                       .arg(r.hideRange, 0, 'f', 2)
                       .arg(r.resurrectRange, 0, 'f', 2)
                       .arg(QString::fromUtf8(wolfSourceName(source)));
    for (const auto& k : r.results) {
        line += QString("\n    #%1: %2 → %3 (разница %4, был %5)")
                    .arg(k.kidId)
                    .arg(k.number)
                    .arg(outcomeText(k.outcome))
                    .arg(k.diff)
                    .arg(k.wasAlive ? "жив" : "мёртв");
    }
    line += QString("\n    итог: спрятались %1, попались %2, воскресли %3, мертвы %4")
                .arg(r.hidden)
                .arg(r.caught)
                .arg(r.resurrected)
                .arg(r.dead);
    history_->appendPlainText(line);
}

}
