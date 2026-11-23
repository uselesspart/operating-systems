#include "gamethread.h"

#include <QDeadlineTimer>
#include <QMutexLocker>
#include <random>

#include "conn.h"
#include "logger.h"
#include "protocol.h"
#include "rules.h"

using namespace proto;

GameThread::GameThread(Conn* conn, QObject* parent) : QThread(parent), conn_(conn) {}

void GameThread::RequestStop() {
    stop_ = true;
    QMutexLocker l(&mutex_);
    cond_.wakeAll();
}

void GameThread::SubmitWolfNumber(int n) {
    QMutexLocker l(&mutex_);
    if (!waiting_) return;
    wolfValue_ = qBound(1, n, 100);
    cond_.wakeAll();
}

int GameThread::WaitWolfNumber(int round) {
    {
        QMutexLocker l(&mutex_);
        waiting_ = true;
        wolfValue_ = 0;
    }
    emit WolfTurn(round);

    QMutexLocker l(&mutex_);
    QDeadlineTimer deadline(kWolfInputTimeoutMs);
    while (wolfValue_ == 0 && !stop_) {
        if (!cond_.wait(&mutex_, deadline)) break;
    }
    waiting_ = false;
    if (wolfValue_ != 0) return wolfValue_;

    static thread_local std::mt19937 rng{std::random_device{}()};
    int n = std::uniform_int_distribution<int>(1, 100)(rng);
    if (!stop_) {
        emit Status(QString("Число не введено за 3 сек — волк выбрал случайно: %1").arg(n));
        Log("wolf", "Число не введено, выбрано случайно: %d", n);
    }
    return n;
}

void GameThread::run() {
    emit Status("Ожидание подключения козлёнка...");
    Msg msg{};
    if (!conn_->Read(&msg, sizeof(msg)) || msg.type != kJoin) {
        Log("wolf", "Козлёнок не подключился");
        emit GameOver("Козлёнок не подключился (таймаут или ошибка связи)");
        return;
    }
    emit Status("Козлёнок присоединился");
    Log("wolf", "Козлёнок присоединился");

    bool alive = true;
    Msg reply{kStatus, 1};
    if (!conn_->Write(&reply, sizeof(reply))) {
        emit GameOver("Связь с козлёнком потеряна");
        return;
    }

    int round = 0;
    rules::DeadStreak deadStreak;
    for (;;) {
        ++round;
        if (!conn_->Read(&msg, sizeof(msg)) || msg.type != kNumber) {
            Log("wolf", "Связь с козлёнком потеряна (раунд %d)", round);
            emit Status("Козлёнок отключился или не ответил за 5 сек");
            emit GameOver("Связь прервана");
            return;
        }
        const int kid = msg.value;
        const int wolf = WaitWolfNumber(round);

        if (stop_) {
            reply = Msg{kGameOver, alive ? 1 : 0};
            conn_->Write(&reply, sizeof(reply));
            emit GameOver("Игра остановлена");
            return;
        }

        const rules::Outcome outcome = rules::Judge(alive, kid, wolf);
        alive = rules::AliveAfter(outcome);
        const bool over = deadStreak.Update(alive);

        Log("wolf", "Раунд %d: волк=%d козлёнок=%d, козлёнок %s", round, wolf, kid,
            rules::OutcomeName(outcome));
        emit RoundFinished(round, wolf, kid, static_cast<int>(outcome));

        reply = Msg{over ? kGameOver : kStatus, alive ? 1 : 0};
        if (!conn_->Write(&reply, sizeof(reply))) {
            emit Status("Не удалось отправить статус козлёнку");
            emit GameOver("Связь прервана");
            return;
        }
        if (over) {
            emit GameOver(QString("Козлёнок мёртв %1 хода подряд — игра окончена").arg(kMaxDeadRounds));
            return;
        }
    }
}
