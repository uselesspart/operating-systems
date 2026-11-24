#include "gui/main_window.hpp"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStringList>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace lab2::gui {
namespace {

QString stateText(game::KidState state) {
    return state == game::KidState::Alive ? "Alive" : "Dead";
}

QString outcomeText(game::KidOutcome outcome) {
    switch (outcome) {
    case game::KidOutcome::Hidden:
        return "Hidden";
    case game::KidOutcome::Caught:
        return "Caught";
    case game::KidOutcome::Revived:
        return "Revived";
    case game::KidOutcome::RemainedDead:
        return "Remained dead";
    }
    return "Unknown";
}

QString unsignedText(std::uint64_t value) {
    return QString::number(static_cast<qulonglong>(value));
}

void setCell(QTableWidget* table, int row, int column, const QString& value) {
    auto* item = table->item(row, column);
    if (item == nullptr) {
        table->setItem(row, column, new QTableWidgetItem(value));
    } else {
        item->setText(value);
    }
}

void configureTable(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(static_cast<int>(headers.size()));
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

} // namespace

MainWindow::MainWindow(host::HostController& controller, const QString& transport, pid_t hostPid,
                       QWidget* parent)
    : QMainWindow(parent), controller_(controller) {
    setWindowTitle("Wolf and Seven Kids - Host");
    resize(960, 760);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(new QLabel(
        QString("IPC: %1 | Host PID: %2").arg(transport).arg(static_cast<qlonglong>(hostPid))));

    auto* overview = new QHBoxLayout;
    status_ = new QLabel("Waiting for clients");
    status_->setObjectName("statusLabel");
    clientsCount_ = new QLabel("Clients: 0");
    clientsCount_->setObjectName("clientsCount");
    roundLabel_ = new QLabel("Round: -");
    roundLabel_->setObjectName("roundLabel");
    overview->addWidget(status_, 1);
    overview->addWidget(clientsCount_);
    overview->addWidget(roundLabel_);
    layout->addLayout(overview);

    auto* input = new QHBoxLayout;
    input->addWidget(new QLabel("Wolf number (1-100):"));
    number_ = new QSpinBox;
    number_->setObjectName("wolfNumber");
    number_->setRange(game::kMinNumber, game::kMaxWolfNumber);
    number_->setValue(50);
    submit_ = new QPushButton("Submit wolf number");
    submit_->setObjectName("submitButton");
    input->addWidget(number_);
    input->addWidget(submit_);
    input->addStretch();
    countdownLabel_ = new QLabel("Input closed");
    countdownLabel_->setObjectName("countdownLabel");
    input->addWidget(countdownLabel_);
    layout->addLayout(input);
    countdown_ = new QProgressBar;
    countdown_->setObjectName("countdown");
    countdown_->setRange(0, 3000);
    countdown_->setTextVisible(false);
    countdown_->setValue(0);
    layout->addWidget(countdown_);

    layout->addWidget(new QLabel("Connected clients (last completed move)"));
    clients_ = new QTableWidget;
    clients_->setObjectName("clientsTable");
    configureTable(clients_, {"ID", "PID", "Number", "State", "Outcome"});
    layout->addWidget(clients_, 2);
    statistics_ = new QLabel("No completed rounds");
    statistics_->setObjectName("statisticsLabel");
    statistics_->setWordWrap(true);
    layout->addWidget(statistics_);

    layout->addWidget(new QLabel("Round history"));
    history_ = new QTableWidget;
    history_->setObjectName("historyTable");
    configureTable(history_, {"Round", "Wolf", "Initial n", "Hidden", "Caught", "Revived", "Dead"});
    layout->addWidget(history_, 2);
    layout->addWidget(new QLabel("Event log (including individual moves)"));
    log_ = new QPlainTextEdit;
    log_->setObjectName("eventLog");
    log_->setReadOnly(true);
    layout->addWidget(log_, 2);
    stop_ = new QPushButton("Stop game");
    stop_->setObjectName("stopButton");
    layout->addWidget(stop_);
    setCentralWidget(central);

    connect(submit_, &QPushButton::clicked, this, [this] {
        if (controller_.submitWolfNumber(currentRound_, number_->value())) {
            submitted_ = true;
            appendLog(QString("Wolf number submitted: %1").arg(number_->value()));
        } else {
            appendLog("Input rejected: round ended or deadline expired");
        }
        refreshInput();
    });
    connect(stop_, &QPushButton::clicked, this, [this] { stopGame(); });
    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, [this] {
        for (const auto& event : controller_.drainEvents()) {
            applyEvent(event);
        }
        refreshInput();
    });
    refreshInput();
    timer_->start(20);
}

void MainWindow::appendLog(const QString& message) {
    log_->appendPlainText(message);
}

int MainWindow::findClientRow(game::ClientId clientId) const {
    const auto id = unsignedText(clientId);
    for (int row = 0; row < clients_->rowCount(); ++row) {
        if (clients_->item(row, 0)->text() == id) {
            return row;
        }
    }
    return -1;
}

void MainWindow::updateClientCount() {
    clientsCount_->setText(QString("Clients: %1").arg(clients_->rowCount()));
}

void MainWindow::displayRound(const game::RoundResult& result) {
    if (!result.completed) {
        statistics_->setText("Round cancelled: no remaining participants");
        return;
    }
    for (const auto& kid : result.kids) {
        const int row = findClientRow(kid.clientId);
        if (row >= 0) {
            setCell(clients_, row, 2, QString::number(kid.number));
            setCell(clients_, row, 3, stateText(kid.currentState));
            setCell(clients_, row, 4, outcomeText(kid.outcome));
        }
        appendLog(QString("  Client %1: %2 | %3 -> %4 (%5)")
                      .arg(unsignedText(kid.clientId))
                      .arg(kid.number)
                      .arg(stateText(kid.previousState))
                      .arg(stateText(kid.currentState))
                      .arg(outcomeText(kid.outcome)));
    }
    const int row = history_->rowCount();
    history_->insertRow(row);
    setCell(history_, row, 0, unsignedText(result.roundNumber));
    setCell(history_, row, 1, QString::number(result.wolfNumber));
    setCell(history_, row, 2, unsignedText(result.initialKidCount));
    setCell(history_, row, 3, unsignedText(result.hiddenCount));
    setCell(history_, row, 4, unsignedText(result.caughtCount));
    setCell(history_, row, 5, unsignedText(result.revivedCount));
    setCell(history_, row, 6, unsignedText(result.deadCount));
    history_->scrollToBottom();
    statistics_->setText(QString("Wolf: %1 | Hidden: %2 | Caught: %3 | Revived: %4 | Alive: %5 | "
                                 "Dead: %6 | All-dead streak: %7/2")
                             .arg(result.wolfNumber)
                             .arg(unsignedText(result.hiddenCount))
                             .arg(unsignedText(result.caughtCount))
                             .arg(unsignedText(result.revivedCount))
                             .arg(unsignedText(result.aliveCount))
                             .arg(unsignedText(result.deadCount))
                             .arg(unsignedText(result.consecutiveAllDeadRounds)));
}

void MainWindow::applyEvent(const host::HostEvent& event) {
    using host::HostEventType;
    appendLog(QString::fromStdString(event.text));
    switch (event.type) {
    case HostEventType::ClientJoined: {
        if (findClientRow(event.clientId) >= 0) {
            break;
        }
        const int row = clients_->rowCount();
        clients_->insertRow(row);
        setCell(clients_, row, 0, unsignedText(event.clientId));
        setCell(clients_, row, 1, QString::number(static_cast<qlonglong>(event.clientPid)));
        setCell(clients_, row, 2, "-");
        setCell(clients_, row, 3, "Alive");
        setCell(clients_, row, 4, "Not played");
        updateClientCount();
        break;
    }
    case HostEventType::ClientLeft: {
        const int row = findClientRow(event.clientId);
        if (row >= 0) {
            clients_->removeRow(row);
        }
        updateClientCount();
        break;
    }
    case HostEventType::RoundStarted:
        currentRound_ = event.roundNumber;
        submitted_ = false;
        roundLabel_->setText(QString("Round: %1").arg(unsignedText(currentRound_)));
        if (!stopping_ && !finished_) {
            status_->setText("Round in progress");
        }
        break;
    case HostEventType::RoundFinished:
        submitted_ = true;
        if (event.result) {
            displayRound(*event.result);
            if (event.result->gameOver) {
                finished_ = true;
                status_->setText("Game over");
            }
        }
        break;
    case HostEventType::Status:
        if (!stopping_ && !finished_) {
            status_->setText(QString::fromStdString(event.text));
        }
        break;
    case HostEventType::GameFinished:
        finished_ = true;
        status_->setText("Game over");
        break;
    case HostEventType::Stopped:
        stopping_ = true;
        if (!finished_) {
            status_->setText("Host stopped");
        }
        break;
    case HostEventType::Error:
        break;
    }
    refreshInput();
}

void MainWindow::refreshInput() {
    // Контроллер владеет реальным дедлайном; GUI лишь отображает его остаток.
    const auto remaining = controller_.inputTimeRemaining(currentRound_).count();
    const bool enabled = !stopping_ && !finished_ && !submitted_ && remaining > 0;
    number_->setEnabled(enabled);
    submit_->setEnabled(enabled);
    stop_->setEnabled(!stopping_ && !finished_);
    const int millis = enabled ? static_cast<int>(std::min<std::int64_t>(remaining, 3000)) : 0;
    countdown_->setValue(millis);
    countdownLabel_->setText(enabled ? QString("%1 s").arg(millis / 1000.0, 0, 'f', 1)
                                     : QString("Input closed"));
}

void MainWindow::stopGame() {
    if (stopping_ || finished_) {
        return;
    }
    stopping_ = true;
    status_->setText("Stopping...");
    controller_.requestStop();
    refreshInput();
}

} // namespace lab2::gui
