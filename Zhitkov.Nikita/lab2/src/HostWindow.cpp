#include "HostWindow.h"
#include "Protocol.h"
#include "WolfInputDialog.h"
#include <QVBoxLayout>
#include <QWidget>
#include <cstdlib>
#include <ctime>

HostWindow::HostWindow(Conn* conn, SharedSems* sems, QWidget* parent)
    : QMainWindow(parent), conn_(conn), sems_(sems), scenario_(1)
{
    std::srand(std::time(nullptr));
    setWindowTitle("Волк и козлёнок (lab2, вариант 3)");
    resize(700, 500);

    auto* central = new QWidget(this);
    auto* layout  = new QVBoxLayout(central);

    header_ = new QLabel("Статус: ожидание клиента...", this);
    header_->setStyleSheet("font-weight: bold; padding: 4px;");
    layout->addWidget(header_);

    log_ = new QTextEdit(this);
    log_->setReadOnly(true);
    layout->addWidget(log_);
    setCentralWidget(central);

    connect(this, &HostWindow::logAppend,   this, &HostWindow::onLogAppend,   Qt::QueuedConnection);
    connect(this, &HostWindow::roundFinished, this, &HostWindow::onRoundFinished, Qt::QueuedConnection);
    connect(this, &HostWindow::needWolfInput, this, &HostWindow::onNeedWolfInput, Qt::QueuedConnection);
    
    emit logAppend("Хост запущен");
    emit logAppend("Клиент (козлёнок) присоединился");
    thread_ = std::thread([this] { gameLoop(); });
}

HostWindow::~HostWindow() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void HostWindow::onLogAppend(const QString& msg) { log_->append(msg); }

void HostWindow::onRoundFinished(int round, int wolf, int kid,
                                 const QString& status, bool alive_after,
                                 int hid, int caught, int resurrected) {
    log_->append(QString("Раунд %1: волк = %2, козлёнок = %3 -> %4")
                 .arg(round).arg(wolf).arg(kid).arg(status));
    log_->append(QString("  Жив: %1 | Спрятался: %2 | Пойман: %3 | Воскрес: %4")
                 .arg(alive_after ? "да" : "нет").arg(hid).arg(caught).arg(resurrected));
    header_->setText(QString("Раунд %1 | Спрятался: %2 | Пойман: %3 | Воскрес: %4")
                     .arg(round).arg(hid).arg(caught).arg(resurrected));
}

void HostWindow::onNeedWolfInput() {
    WolfInputDialog dlg(this);
    dlg.exec();
    if (pending_promise_) {
        pending_promise_->set_value(dlg.value());
        pending_promise_ = nullptr;
    }
}

int HostWindow::getWolfNum() {
    std::promise<int> promise;
    auto future = promise.get_future();
    pending_promise_ = &promise;
    emit needWolfInput();
    return future.get();
}

void HostWindow::gameLoop() {
    while (running_ && !scenario_.gameOver()) {
        const int wolf = getWolfNum();

        // Отправить число волка
        HostToClient wolfMsg{};
        wolfMsg.type  = WolfNumber;
        wolfMsg.value = wolf;
        if (!conn_->Write(&wolfMsg, sizeof(wolfMsg))) {
            emit logAppend("Ошибка: не удалось отправить число волка клиенту");
            return;
        }
        sem_post(&sems_->host_msg_ready);

        // Дождаться числа козлёнка
        if (!semWaitTimeout(&sems_->client_msg_ready, 5)) {
            emit logAppend("Таймаут ожидания числа козлёнка");
            return;
        }
        ClientToHost kidMsg{};
        if (!conn_->Read(&kidMsg, sizeof(kidMsg))) {
            emit logAppend("Ошибка: не удалось прочитать число козлёнка");
            return;
        }
        if (kidMsg.type != KidNumber) {
            emit logAppend("Ошибка: неверный тип сообщения от клиента");
            return;
        }

        // Обработать раунд
        RoundResult r = scenario_.playRound(wolf, kidMsg.value);

        // Ответить клиенту статусом
        HostToClient statusMsg{};
        statusMsg.type  = Status;
        statusMsg.value = r.kid_alive_after ? 1 : 0;
        if (!conn_->Write(&statusMsg, sizeof(statusMsg))) {
            emit logAppend("Ошибка: не удалось отправить статус клиенту");
            return;
        }
        sem_post(&sems_->host_msg_ready);

        // Обновить GUI
        QString statusStr;
        switch (r.kid_status) {
            case RoundResult::Status::Hid:         statusStr = "спрятался";     break;
            case RoundResult::Status::Caught:      statusStr = "пойман";        break;
            case RoundResult::Status::Resurrected: statusStr = "воскрес";       break;
            case RoundResult::Status::Dead:        statusStr = "остался мёртв"; break;
        }
        emit roundFinished(r.round, r.wolf_num, r.kid_num, statusStr,
                           r.kid_alive_after,
                           scenario_.totalHid(),
                           scenario_.totalCaught(),
                           scenario_.totalResurrected());
    }
    emit logAppend("ИГРА ЗАВЕРШЕНА");
}