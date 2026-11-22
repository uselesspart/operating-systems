#include "host/HostServer.h"

#include <pthread.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

#include "client/KidMain.h"
#include "conn/conn.h"
#include "ipc/LocalSemaphore.h"
#include "ipc/Names.h"
#include "util/Log.h"

namespace wolf {

namespace {

std::string kidName(int id, pid_t pid) {
    return "козлёнок #" + std::to_string(id) + " (pid " + std::to_string(pid) + ")";
}

std::string fmt2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

std::string describeRound(const game::RoundResult& r, WolfSource source) {
    std::string s = "раунд " + std::to_string(r.round) + ": волк " + std::to_string(r.wolf) +
                    " (" + wolfSourceName(source) + ")" + ", козлят " +
                    std::to_string(r.kids) + ", порог укрытия " + fmt2(r.hideRange) +
                    ", порог воскрешения " + fmt2(r.resurrectRange);
    for (const auto& k : r.results) {
        s += " | #" + std::to_string(k.kidId) + " " + std::to_string(k.number) + " " +
             (k.wasAlive ? "жив" : "мёртв") + "→" + proto::outcomeName(k.outcome);
    }
    s += " | спрятались " + std::to_string(r.hidden) + ", попались " + std::to_string(r.caught) +
         ", воскресли " + std::to_string(r.resurrected) + ", мертвы " + std::to_string(r.dead);
    return s;
}

}

HostServer::HostServer(HostOptions options, HostObserver& observer)
    : options_(std::move(options)), observer_(observer), hostPid_(::getpid()) {
    rng_.seed(options_.seed ? *options_.seed : std::random_device{}());
}

HostServer::~HostServer() { stop(); }

void HostServer::start() {
    if (started_) return;
    try {
        sems_ = sem::createShared(2 * kMaxKids);
    } catch (const std::system_error& e) {
        log::error(std::string("не удалось создать локальные семафоры: ") + e.what());
        return;
    }

    started_ = true;
    log::info("хост запущен: pid " + std::to_string(hostPid_) + ", канал " + Conn::typeCode() +
              ", козлят " + std::to_string(options_.kids) + ", ход волка " +
              std::to_string(options_.turn.count()) + " мс");
    status("Волк вышел на охоту. Козлят: " + std::to_string(options_.kids) +
           ". Кнопка «Добавить козлёнка» порождает ещё одного потомка.");

    for (int i = 0; i < options_.kids; ++i) spawnKid(true);
    for (auto& [id, session] : sessions_) session->start();

    reapThread_ = std::thread(&HostServer::reapLoop, this);
    signalThread_ = std::thread(&HostServer::signalLoop, this);
    gameThread_ = std::thread(&HostServer::gameLoop, this);
}

void HostServer::stop() {
    if (!started_) return;
    started_ = false;
    stopping_ = true;
    cv_.notify_all();

    if (gameThread_.joinable()) gameThread_.join();

    std::map<int, std::unique_ptr<KidSession>> sessions;
    std::vector<std::unique_ptr<KidSession>> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sessions.swap(sessions_);
        finished.swap(finished_);
    }

    for (auto& [id, session] : sessions) session->send(proto::makeBye(), false);
    for (auto& [id, session] : sessions) session->stop();
    for (auto& [id, session] : sessions) session->join();

    const auto natural = std::chrono::steady_clock::now() + kStopGrace;
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (children_.empty()) break;
        }
        if (std::chrono::steady_clock::now() >= natural) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (pid_t pid : children_) ::kill(pid, SIGTERM);
    }

    const auto forced = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    for (;;) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (children_.empty()) break;
        }
        if (std::chrono::steady_clock::now() >= forced) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (pid_t pid : children_) ::kill(pid, SIGKILL);
    }

    if (reapThread_.joinable()) reapThread_.join();
    if (signalThread_.joinable()) signalThread_.join();

    sessions.clear();
    finished.clear();

    sem::destroyShared(sems_, 2 * kMaxKids);
    sems_ = nullptr;
    ::rmdir(names::runtimeDir(hostPid_).c_str());
    log::info("хост завершил работу, каналы и семафоры удалены");
}

void HostServer::addKid() {
    if (!started_ || stopping_) return;
    if (spawnKid(false) == 0) status("Не удалось добавить козлёнка");
}

int HostServer::spawnKid(bool deferStart) {
    std::lock_guard<std::mutex> spawnLock(spawnMutex_);

    int id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || nextId_ > kMaxKids) return 0;
        id = nextId_++;
    }

    const ConnId cid{hostPid_, id};
    try {
        Conn::prepare(cid);
    } catch (const std::system_error& e) {
        log::error("не удалось подготовить канал для козлёнка #" + std::to_string(id) + ": " +
                   e.what());
        return 0;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        Conn::discard(cid);
        log::error("fork не удался для козлёнка #" + std::to_string(id));
        return 0;
    }
    if (pid == 0) {
        childMain(cid);
        ::_exit(1);
    }

    std::unique_ptr<Conn> conn;
    try {
        conn = std::make_unique<Conn>(cid, true);
    } catch (const std::system_error& e) {
        Conn::discard(cid);
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
        log::error("не удалось открыть канал для козлёнка #" + std::to_string(id) + ": " + e.what());
        return 0;
    }

    sem_t* toKid = sems_ + 2 * (id - 1);
    sem_t* toHost = toKid + 1;
    auto session = std::make_unique<KidSession>(
        cid, pid, std::move(conn), toKid, toHost,
        [this](int kid, const proto::Message& message) { handleReply(kid, message); },
        [this](int kid, const std::string& reason) { handleFailure(kid, reason); });
    KidSession* rawSession = session.get();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        children_.insert(pid);
        idByPid_[pid] = id;
        sessions_[id] = std::move(session);
        info_[id] = KidInfo{id, pid, true, 0, proto::Outcome::None};
        game_.addKid(id);
    }

    if (!deferStart) rawSession->start();

    const std::string text = kidName(id, pid) + " присоединился";
    log::info(text);
    observer_.onStatus(text);
    notifyKids();
    cv_.notify_all();
    return id;
}

void HostServer::childMain(const ConnId& id) {
    ::signal(SIGPIPE, SIG_IGN);
    sigset_t empty;
    sigemptyset(&empty);
    ::pthread_sigmask(SIG_SETMASK, &empty, nullptr);

    try {
        log::init("kid_" + std::string(Conn::typeCode()) + "#" + std::to_string(id.clientId));
        Conn conn(id, false);

        KidOptions options;
        options.id = id.clientId;
        options.seed = options_.seed ? (*options_.seed + 7919u * static_cast<unsigned>(id.clientId))
                                     : static_cast<unsigned>(std::random_device{}());

        sem_t* toKid = sems_ + 2 * (id.clientId - 1);
        sem_t* toHost = toKid + 1;
        const int rc = runKid(options, conn, toKid, toHost);
        ::_exit(rc);
    } catch (const std::exception& e) {
        log::error("козлёнок #" + std::to_string(id.clientId) + " не смог подключиться: " +
                   e.what());
        ::_exit(1);
    }
}

void HostServer::signalLoop() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    const timespec poll{0, 200'000'000};

    while (!stopping_) {
        siginfo_t info{};
        const int sig = ::sigtimedwait(&set, &info, &poll);
        if (sig == SIGINT || sig == SIGTERM) {
            log::info(std::string("получен ") + (sig == SIGINT ? "SIGINT" : "SIGTERM") +
                      ", завершаю работу");
            observer_.onTerminateRequested();
            break;
        }
    }
}

void HostServer::reapLoop() {
    for (;;) {
        int status = 0;
        pid_t pid = 0;
        while ((pid = ::waitpid(-1, &status, WNOHANG)) > 0) onChildExit(pid, status);

        bool done = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done = stopping_ && children_.empty();
        }
        if (done) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void HostServer::onChildExit(pid_t pid, int status) {
    int id = 0;
    bool known = false;
    bool hadInfo = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        children_.erase(pid);
        const auto it = idByPid_.find(pid);
        if (it != idByPid_.end()) {
            id = it->second;
            known = true;
            hadInfo = info_.count(id) != 0;
            if (hadInfo) dropKidLocked(id, false);
        }
    }

    std::string text = "потомок pid " + std::to_string(pid) + " завершился";
    if (known) text = kidName(id, pid) + " завершился";
    if (WIFEXITED(status)) {
        text += " (код " + std::to_string(WEXITSTATUS(status)) + ")";
    } else if (WIFSIGNALED(status)) {
        text += " (сигнал " + std::to_string(WTERMSIG(status)) + ")";
    }
    log::info(text);

    if (hadInfo && !stopping_) {
        observer_.onStatus(text);
        notifyKids();
        cv_.notify_all();
    }
}

void HostServer::handleReply(int kidId, const proto::Message& reply) {
    std::string problem;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (reply.type != proto::Type::Throw || reply.round != collectingRound_ ||
            !waiting_.count(kidId)) {
            log::warn("козлёнок #" + std::to_string(kidId) + ": неожиданный ответ " +
                      proto::typeName(reply.type) + " раунда " + std::to_string(reply.round));
            return;
        }
        const int max = game::maxThrow(game_.isAlive(kidId));
        if (reply.number < 1 || reply.number > max) {
            problem = "бросок " + std::to_string(reply.number) + " вне диапазона 1.." +
                      std::to_string(max);
        } else {
            throws_[kidId] = reply.number;
            waiting_.erase(kidId);
        }
    }
    if (!problem.empty()) {
        handleFailure(kidId, problem);
        return;
    }
    cv_.notify_all();
}

void HostServer::handleFailure(int kidId, const std::string& reason) {
    pid_t pid = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = info_.find(kidId);
        if (it == info_.end()) return;
        pid = it->second.pid;
        dropKidLocked(kidId);
    }
    const std::string text = kidName(kidId, pid) + " отключился: " + reason;
    log::warn(text);
    if (!stopping_) {
        observer_.onStatus(text);
        notifyKids();
        cv_.notify_all();
    }
}

void HostServer::dropKidLocked(int kidId, bool killChild) {
    const auto it = sessions_.find(kidId);
    if (it != sessions_.end()) {
        const pid_t pid = it->second->pid();
        if (killChild) ::kill(pid, SIGTERM);
        it->second->stop();
        idByPid_.erase(pid);
        finished_.push_back(std::move(it->second));
        sessions_.erase(it);
    }
    info_.erase(kidId);
    game_.removeKid(kidId);
    waiting_.erase(kidId);
    throws_.erase(kidId);
}

std::vector<KidInfo> HostServer::kidsLocked() const {
    std::vector<KidInfo> kids;
    for (const auto& [id, info] : info_) {
        KidInfo k = info;
        k.alive = game_.isAlive(id);
        kids.push_back(k);
    }
    return kids;
}

void HostServer::reapFinishedLocked(std::unique_lock<std::mutex>& lock) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second->finished()) {
            finished_.push_back(std::move(it->second));
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<std::unique_ptr<KidSession>> done;
    for (auto it = finished_.begin(); it != finished_.end();) {
        if ((*it)->finished()) {
            done.push_back(std::move(*it));
            it = finished_.erase(it);
        } else {
            ++it;
        }
    }
    if (done.empty()) return;

    lock.unlock();
    done.clear();
    lock.lock();
}

void HostServer::notifyKids() {
    std::vector<KidInfo> kids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        kids = kidsLocked();
    }
    observer_.onKids(kids);
}

void HostServer::status(const std::string& text) {
    log::info(text);
    observer_.onStatus(text);
}

void HostServer::submitWolfNumber(int number) {
    if (number < 1 || number > game::kWolfMax) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        wolfInput_ = number;
    }
    cv_.notify_all();
}

void HostServer::gameLoop() {
    using Clock = std::chrono::steady_clock;
    std::unique_lock<std::mutex> lock(mutex_);
    bool inGame = false;

    while (!stopping_) {
        reapFinishedLocked(lock);

        if (game_.kidCount() == 0) {
            if (inGame) {
                inGame = false;
                game_.clear();
                lock.unlock();
                status("Все козлята ушли — игра прервана, ждём новых");
                lock.lock();
            }
            cv_.wait_for(lock, std::chrono::milliseconds(200));
            continue;
        }

        if (!inGame) {
            inGame = true;
            game_.reset();
            const int n = game_.kidCount();
            lock.unlock();
            status("Игра началась, козлят: " + std::to_string(n));
            notifyKids();
            lock.lock();
        }

        const int round = game_.round() + 1;
        lock.unlock();
        observer_.onTurnStarted(round, options_.turn);
        lock.lock();
        cv_.wait_until(lock, Clock::now() + options_.turn,
                       [this] { return stopping_ || wolfInput_.has_value(); });
        if (stopping_) break;

        WolfSource source;
        int wolf = 0;
        if (wolfInput_) {
            source = WolfSource::User;
            wolf = *wolfInput_;
            wolfInput_.reset();
        } else if (options_.fixedWolf) {
            source = WolfSource::Fixed;
            wolf = *options_.fixedWolf;
        } else {
            source = WolfSource::Random;
            wolf = std::uniform_int_distribution<int>(1, game::kWolfMax)(rng_);
        }

        if (game_.kidCount() == 0) continue;

        const auto n = static_cast<std::uint32_t>(game_.kidCount());
        collectingRound_ = static_cast<unsigned>(round);
        throws_.clear();
        waiting_.clear();
        for (auto& [id, session] : sessions_) {
            if (!game_.hasKid(id)) continue;
            waiting_.insert(id);
            session->send(proto::makeRound(collectingRound_, game_.isAlive(id), n), true);
        }
        cv_.wait_until(lock, Clock::now() + kRoundCollect,
                       [this] { return stopping_ || waiting_.empty(); });
        if (stopping_) break;
        collectingRound_ = 0;

        const std::set<int> late = waiting_;
        for (int id : late) {
            const pid_t pid = info_.count(id) ? info_[id].pid : 0;
            dropKidLocked(id);
            lock.unlock();
            log::warn(kidName(id, pid) + " не бросил за " +
                      std::to_string(kRoundCollect.count() / 1000) + " с — исключён");
            observer_.onStatus(kidName(id, pid) + " не успел бросить — исключён из игры");
            lock.lock();
        }

        if (game_.kidCount() == 0) continue;

        std::vector<game::Throw> throws;
        for (const auto& [id, number] : throws_) throws.push_back({id, number});
        const game::RoundResult result = game_.playRound(wolf, throws);

        for (const auto& k : result.results) {
            proto::Message message;
            message.type = proto::Type::Result;
            message.round = static_cast<std::uint32_t>(result.round);
            message.number = k.number;
            message.wolf = result.wolf;
            message.alive = k.aliveAfter ? 1 : 0;
            message.outcome = k.outcome;
            message.kids = static_cast<std::uint32_t>(result.kids);
            message.hidden = static_cast<std::uint32_t>(result.hidden);
            message.caught = static_cast<std::uint32_t>(result.caught);
            message.dead = static_cast<std::uint32_t>(result.dead);
            const auto session = sessions_.find(k.kidId);
            if (session != sessions_.end()) session->second->send(message, false);
            const auto info = info_.find(k.kidId);
            if (info != info_.end()) {
                info->second.lastNumber = k.number;
                info->second.lastOutcome = k.outcome;
            }
        }

        lock.unlock();
        log::info(describeRound(result, source));
        observer_.onRound(result, source);
        notifyKids();
        lock.lock();

        if (result.gameOver) {
            for (auto& [id, session] : sessions_) {
                if (game_.hasKid(id)) {
                    session->send(proto::makeGameOver(static_cast<std::uint32_t>(result.round)),
                                  false);
                }
            }
            game_.clear();
            inGame = false;
            lock.unlock();
            status("Игра окончена на раунде " + std::to_string(result.round) +
                   ": все козлята мертвы " + std::to_string(game::kAllDeadRoundsToEnd) +
                   " хода подряд. Добавьте козлят для новой игры");
            observer_.onGameOver(result.round);
            notifyKids();
            lock.lock();
        }
    }
}

}
