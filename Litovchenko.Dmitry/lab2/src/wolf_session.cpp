#include "wolf_session.h"

#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#include <exception>
#include <memory>

#include "conn.h"

WolfSession::WolfSession(Shared* shared, pid_t goat, int id, Callbacks callbacks)
    : shared_(shared), goat_(goat), id_(id), cb_(std::move(callbacks)) {}

void WolfSession::status(const std::string& text) {
    logMsg("%s", text.c_str());
    if (cb_.status) cb_.status(text);
}

int WolfSession::run(int maxRounds, const std::atomic<bool>& stop) {
    status(std::string("ожидание козлёнка, связь: ") + Conn::Type());
    std::unique_ptr<Conn> conn;
    try {
        conn = std::make_unique<Conn>(id_, true);
    } catch (const std::exception& e) {
        status(std::string("козлёнок не подключился: ") + e.what());
        reapGoat(true);
        return 1;
    }
    conn_ = conn.get();
    status("козлёнок подключился, pid " + std::to_string(goat_));

    Game game(1);
    int code = 0;
    for (int round = 1;; ++round) {
        if (stop || (maxRounds > 0 && round > maxRounds)) {
            status("игра остановлена");
            if (!endGame()) code = 1;
            break;
        }
        WolfChoice wolf = cb_.wolfNumber(round);
        if (stop) continue;

        GoatMsg msg{};
        if (!waitSem(&shared_->hostCanRead)) {
            status("козлёнок не ответил за 5 с, связь разорвана");
            code = 1;
            break;
        }
        if (!conn->Read(&msg, sizeof(msg)) || msg.number < 1 || msg.number > 100) {
            status("ошибка чтения числа козлёнка");
            code = 1;
            break;
        }
        RoundResult result = game.play(wolf.number, {msg.number});
        result.wolfManual = wolf.manual;
        WolfMsg reply{game.alive(0), game.over()};
        if (!conn->Write(&reply, sizeof(reply))) {
            status("ошибка отправки ответа козлёнку");
            code = 1;
            break;
        }
        sem_post(&shared_->goatCanRead);
        if (cb_.roundDone) cb_.roundDone(result);
        if (game.over()) {
            status("игра окончена: козлёнок мёртв два хода подряд");
            break;
        }
    }
    conn.reset();
    conn_ = nullptr;
    reapGoat(code != 0);
    return code;
}

bool WolfSession::endGame() {
    GoatMsg msg{};
    WolfMsg reply{0, 1};
    if (!waitSem(&shared_->hostCanRead) || !conn_->Read(&msg, sizeof(msg)) ||
        !conn_->Write(&reply, sizeof(reply))) {
        status("козлёнок не ответил при завершении игры");
        return false;
    }
    sem_post(&shared_->goatCanRead);
    return true;
}

void WolfSession::reapGoat(bool force) {
    if (force) {
        kill(goat_, SIGTERM);
        kill(goat_, SIGCONT);
    }
    int wstatus = 0;
    for (int i = 0; i < kTimeoutSec * 10; ++i) {
        pid_t done = waitpid(goat_, &wstatus, WNOHANG);
        if (done == goat_) {
            if (WIFEXITED(wstatus)) {
                status("козлёнок завершился, код " + std::to_string(WEXITSTATUS(wstatus)));
            } else if (WIFSIGNALED(wstatus)) {
                status("козлёнок остановлен сигналом " + std::to_string(WTERMSIG(wstatus)));
            }
            return;
        }
        timespec pause{0, 100 * 1000 * 1000};
        nanosleep(&pause, nullptr);
    }
    kill(goat_, SIGKILL);
    waitpid(goat_, nullptr, 0);
    status("козлёнок не завершился сам и был убит");
}
