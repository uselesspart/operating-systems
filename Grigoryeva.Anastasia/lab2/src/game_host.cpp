#include "game_host.h"

#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <sstream>

#include "conn.h"
#include "handshake.h"
#include "protocol.h"

using namespace std::chrono;

// Козлёнок на стороне хоста: соединение + рабочий поток, который с ним общается.
struct GameHost::Kid {
    int id = 0;
    pid_t pid = 0;
    std::unique_ptr<Conn> conn;
    std::thread thread;

    std::mutex m;  // защищает job/stop/exited и параметры задания
    std::condition_variable cv;
    bool job = false;
    bool stop = false;
    bool exited = false;
    int jobWolf = 0;
    int jobKids = 0;

    std::atomic<bool> ready{false};
    std::atomic<bool> connected{true};
    std::atomic<bool> alive{true};
    std::atomic<int> lastNumber{0};

    // результат последнего задания (пишет рабочий поток до уменьшения pending_, читает игровой поток после)
    bool resultValid = false;
    int resultNumber = 0;
    bool resultWasAlive = true;
    rules::Outcome resultOutcome = rules::Outcome::Hidden;
};

std::string FormatRound(const RoundReport& r) {
    std::ostringstream os;
    os << "Раунд " << r.round << ": волк=" << r.wolf << ", козлят=" << r.kids << ", спрятались=" << r.hidden
       << ", попались=" << r.caught << ", мертвы=" << r.dead << ", воскресли=" << r.revived;
    for (const KidResult& k : r.results) {
        os << "\n    козленок #" << k.id << " (pid " << k.pid << "): число " << k.number << " -> "
           << rules::OutcomeName(k.outcome);
    }
    return os.str();
}

GameHost::GameHost(IGameView& view, const GameConfig& cfg) : view_(view), cfg_(cfg), rng_(std::random_device{}()) {}

GameHost::~GameHost() { Stop(); }

void GameHost::Start() {
    acceptThread_ = std::thread(&GameHost::AcceptLoop, this);
    gameThread_ = std::thread(&GameHost::GameLoop, this);
}

void GameHost::Stop() {
    stopping_ = true;
    // lock-then-notify: исключает потерю уведомления
    { std::lock_guard<std::mutex> l(wm_); }
    wcv_.notify_all();
    { std::lock_guard<std::mutex> l(sleepMutex_); }
    sleepCv_.notify_all();

    if (acceptThread_.joinable()) acceptThread_.join();
    if (gameThread_.joinable()) gameThread_.join();

    std::vector<Kid*> all;
    {
        std::lock_guard<std::mutex> l(kidsMutex_);
        for (auto& k : kids_) all.push_back(k.get());
    }
    for (Kid* k : all) {
        { std::lock_guard<std::mutex> l(k->m); k->stop = true; }
        k->cv.notify_all();
    }
    for (Kid* k : all) {
        if (k->thread.joinable()) k->thread.join();
    }
}

bool GameHost::SubmitWolfNumber(int number) {
    if (number < rules::kMinNumber || number > rules::kMaxWolfNumber) return false;
    std::lock_guard<std::mutex> l(wm_);
    if (!wolfPrompt_ || wolfSubmitted_) return false;
    wolfValue_ = number;
    wolfSubmitted_ = true;
    wcv_.notify_all();
    return true;
}

int GameHost::RandomInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng_); }

void GameHost::SleepInterruptible(int ms) {
    std::unique_lock<std::mutex> l(sleepMutex_);
    sleepCv_.wait_for(l, milliseconds(ms), [this] { return stopping_.load(); });
}

// ---------------------------------------------------------------- знакомство

void GameHost::AcceptLoop() {
    while (!stopping_) {
        pid_t pid = 0;
        if (!HostWaitRequest(300, pid)) continue;
        HandleJoinRequest(pid);
    }
}

void GameHost::HandleJoinRequest(pid_t pid) {
    if (finished_) {  // игра уже окончена - новых козлят не принимаем
        HostReply(pid, -1);
        return;
    }
    // Повторный запрос от уже подключённого клиента (повтор знакомства по тайм-ауту) - отвечаем тем же id
    {
        std::lock_guard<std::mutex> l(kidsMutex_);
        for (auto& k : kids_) {
            if (k->pid == pid && k->connected) {
                HostReply(pid, k->id);
                return;
            }
        }
    }

    std::unique_ptr<Kid> kid(new Kid());
    kid->pid = pid;
    {
        std::lock_guard<std::mutex> l(kidsMutex_);
        kid->id = nextId_++;
    }
    try {
        kid->conn.reset(new Conn(ConnId{kid->id, getpid()}, true));
    } catch (const std::exception& e) {
        view_.OnLog("Ошибка создания соединения для pid " + std::to_string(pid) + ": " + e.what());
        HostReply(pid, -1);
        return;
    }
    Kid* raw = kid.get();
    const int id = raw->id;
    {
        std::lock_guard<std::mutex> l(kidsMutex_);
        kids_.push_back(std::move(kid));
    }
    raw->thread = std::thread(&GameHost::WorkerMain, this, raw);
    HostReply(pid, id);
    view_.OnLog("Козленок #" + std::to_string(id) + " (pid " + std::to_string(pid) +
                "): знакомство выполнено, ожидаю подключение");
}

// ---------------------------------------------------------------- рабочий поток козлёнка

bool GameHost::FailKid(Kid* kid, const std::string& why) {
    view_.OnLog("Козленок #" + std::to_string(kid->id) + ": связь прервана (" + why + ")");
    kid->connected = false;
    return false;
}

void GameHost::WorkerMain(Kid* kid) {
    Message hello;
    if (!kid->conn->Read(&hello, sizeof(hello)) || hello.type != MSG_HELLO) {
        FailKid(kid, "нет приветствия: " + (kid->conn->LastError().empty() ? "неверное сообщение" : kid->conn->LastError()));
        WorkerExit(kid);
        return;
    }
    kid->ready = true;
    view_.OnLog("Козленок #" + std::to_string(kid->id) + " (pid " + std::to_string(kid->pid) + ") присоединился к игре");
    PushKids();

    for (;;) {
        bool doJob = false, doStop = false;
        int wolf = 0, kids = 0;
        {
            std::unique_lock<std::mutex> lk(kid->m);
            kid->cv.wait_for(lk, seconds(1), [&] { return kid->job || kid->stop; });
            doJob = kid->job;
            doStop = kid->stop;
            wolf = kid->jobWolf;
            kids = kid->jobKids;
        }
        if (doJob) {
            bool ok = DoRound(kid, wolf, kids);
            { std::lock_guard<std::mutex> l(kid->m); kid->job = false; }
            { std::lock_guard<std::mutex> l(gm_); --pending_; }
            gcv_.notify_all();
            if (!ok) break;
            continue;
        }
        if (doStop) {
            Message over = MakeMessage(MSG_GAME_OVER);
            kid->conn->Write(&over, sizeof(over));
            break;
        }
        if (!IdleTick(kid)) break;
    }
    WorkerExit(kid);
}

// Козлёнок ждёт своего хода: проверяем, что процесс жив, и шлём PING (хост отвечает "я жив", чтобы
// клиент не оборвал связь по 5-секундному тайм-ауту ожидания).
bool GameHost::IdleTick(Kid* kid) {
    if (kill(kid->pid, 0) != 0 && errno == ESRCH) return FailKid(kid, "процесс завершился");
    Message ping = MakeMessage(MSG_PING);
    if (!kid->conn->Write(&ping, sizeof(ping))) return FailKid(kid, kid->conn->LastError());
    return true;
}

bool GameHost::DoRound(Kid* kid, int wolf, int kids) {
    Message req = MakeMessage(MSG_REQUEST);
    if (!kid->conn->Write(&req, sizeof(req))) return FailKid(kid, kid->conn->LastError());

    Message resp;
    if (!kid->conn->Read(&resp, sizeof(resp))) return FailKid(kid, kid->conn->LastError());
    if (resp.type == MSG_BYE) return FailKid(kid, "козленок завершил работу");
    if (resp.type != MSG_NUMBER) return FailKid(kid, "нарушение протокола");

    const bool wasAlive = kid->alive;
    if (!rules::IsValidKidNumber(wasAlive, resp.value)) {
        return FailKid(kid, "недопустимое число " + std::to_string(resp.value));
    }
    const rules::Outcome outcome = rules::Judge(wasAlive, resp.value, wolf, kids);
    const bool nowAlive = rules::OutcomeIsAlive(outcome);

    Message st = MakeMessage(MSG_STATUS, nowAlive ? kStatusAlive : kStatusDead);
    if (!kid->conn->Write(&st, sizeof(st))) return FailKid(kid, kid->conn->LastError());

    kid->alive = nowAlive;
    kid->lastNumber = resp.value;
    kid->resultNumber = resp.value;
    kid->resultWasAlive = wasAlive;
    kid->resultOutcome = outcome;
    kid->resultValid = true;
    return true;
}

void GameHost::WorkerExit(Kid* kid) {
    {
        std::lock_guard<std::mutex> l(kid->m);
        kid->exited = true;
        if (kid->job) {  // задание успели выдать, но выполнить уже нельзя - освобождаем барьер
            kid->job = false;
            { std::lock_guard<std::mutex> g(gm_); --pending_; }
            gcv_.notify_all();
        }
    }
    const bool wasConnected = kid->connected.exchange(false);
    kid->conn.reset();  // освобождает семафоры/каналы/сокеты
    if (wasConnected) view_.OnLog("Козленок #" + std::to_string(kid->id) + " завершился");
    PushKids();
}

// ---------------------------------------------------------------- игра

std::vector<GameHost::Kid*> GameHost::ReadyKids() {
    std::vector<Kid*> r;
    std::lock_guard<std::mutex> l(kidsMutex_);
    for (auto& k : kids_) {
        if (k->ready && k->connected) r.push_back(k.get());
    }
    return r;
}

std::vector<KidInfo> GameHost::Snapshot() {
    std::vector<KidInfo> r;
    std::lock_guard<std::mutex> l(kidsMutex_);
    for (auto& k : kids_) {
        if (!k->ready) continue;
        r.push_back(KidInfo{k->id, static_cast<int>(k->pid), k->connected, k->alive, k->lastNumber});
    }
    return r;
}

void GameHost::PushKids() { view_.OnKids(Snapshot()); }

int GameHost::ObtainWolfNumber() {
    if (cfg_.fixedWolf > 0) return cfg_.fixedWolf;
    if (!cfg_.interactive) return RandomInt(rules::kMinNumber, rules::kMaxWolfNumber);

    {
        std::lock_guard<std::mutex> l(wm_);
        wolfPrompt_ = true;
        wolfSubmitted_ = false;
    }
    view_.OnWolfPrompt(true, cfg_.wolfTimeoutMs);

    int value = 0;
    bool byUser = false;
    {
        std::unique_lock<std::mutex> l(wm_);
        wcv_.wait_for(l, milliseconds(cfg_.wolfTimeoutMs), [this] { return wolfSubmitted_ || stopping_.load(); });
        byUser = wolfSubmitted_;
        value = wolfValue_;
        wolfPrompt_ = false;
    }
    view_.OnWolfPrompt(false, 0);
    if (!byUser) {
        value = RandomInt(rules::kMinNumber, rules::kMaxWolfNumber);
        view_.OnLog("Время вышло: число волка выбрано случайно: " + std::to_string(value));
    } else {
        view_.OnLog("Волк бросил число " + std::to_string(value));
    }
    return value;
}

RoundReport GameHost::BuildReport(int round, int wolf, const std::vector<Kid*>& kids) {
    RoundReport rep;
    rep.round = round;
    rep.wolf = wolf;
    rep.kids = static_cast<int>(kids.size());
    for (Kid* k : kids) {
        if (!k->resultValid) continue;  // козленок пропал во время хода
        rep.results.push_back(KidResult{k->id, static_cast<int>(k->pid), k->resultNumber, k->resultWasAlive,
                                        k->resultOutcome});
        switch (k->resultOutcome) {
            case rules::Outcome::Hidden: ++rep.hidden; break;
            case rules::Outcome::Caught: ++rep.caught; break;
            case rules::Outcome::Revived: ++rep.revived; break;
            case rules::Outcome::StayedDead: break;
        }
        if (!rules::OutcomeIsAlive(k->resultOutcome)) ++rep.dead;
    }
    return rep;
}

void GameHost::GameLoop() {
    int allDeadStreak = 0;
    int round = 0;
    bool started = false;
    std::string reason = "хост остановлен";

    view_.OnLog("Хост включился (pid " + std::to_string(getpid()) + "), жду козлят: до их подключения игра не начнется");

    while (!stopping_) {
        std::vector<Kid*> kids = ReadyKids();
        const size_t need = started ? 1 : static_cast<size_t>(std::max(1, cfg_.waitKids));
        if (kids.size() < need) {
            SleepInterruptible(100);
            continue;
        }
        if (!started) {
            started = true;
            view_.OnLog("Игра началась, козлят: " + std::to_string(kids.size()));
        }

        ++round;
        view_.OnLog("Раунд " + std::to_string(round) + ": волк выбирает число");
        const int wolf = ObtainWolfNumber();
        if (stopping_) break;

        kids = ReadyKids();  // за время выбора мог кто-то присоединиться или уйти
        if (kids.empty()) {
            --round;
            continue;
        }
        const int n = static_cast<int>(kids.size());

        {
            std::lock_guard<std::mutex> l(gm_);
            pending_ = n;
        }
        for (Kid* k : kids) {
            std::lock_guard<std::mutex> l(k->m);
            k->resultValid = false;
            if (k->exited) {
                std::lock_guard<std::mutex> g(gm_);
                --pending_;
            } else {
                k->jobWolf = wolf;
                k->jobKids = n;
                k->job = true;
                k->cv.notify_one();
            }
        }
        {
            std::unique_lock<std::mutex> l(gm_);
            gcv_.wait(l, [this] { return pending_ <= 0; });
        }

        RoundReport rep = BuildReport(round, wolf, kids);
        view_.OnRound(rep);
        PushKids();

        const bool allDead = !rep.results.empty() && rep.dead == static_cast<int>(rep.results.size());
        allDeadStreak = allDead ? allDeadStreak + 1 : 0;
        if (allDeadStreak >= rules::kAllDeadRoundsToFinish) {
            reason = "все козлята мертвы " + std::to_string(rules::kAllDeadRoundsToFinish) +
                     " хода подряд - волк победил";
            break;
        }
        if (cfg_.maxRounds > 0 && round >= cfg_.maxRounds) {
            reason = "достигнут предел раундов (" + std::to_string(cfg_.maxRounds) + ")";
            break;
        }
        SleepInterruptible(cfg_.roundDelayMs);
    }
    FinishGame(reason);
}

void GameHost::FinishGame(const std::string& reason) {
    {
        std::lock_guard<std::mutex> l(kidsMutex_);
        for (auto& k : kids_) {
            { std::lock_guard<std::mutex> kl(k->m); k->stop = true; }
            k->cv.notify_all();
        }
    }
    finished_ = true;
    view_.OnGameOver(reason);
}
