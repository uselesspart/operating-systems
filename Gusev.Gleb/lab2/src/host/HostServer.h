#pragma once

#include <sys/types.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <thread>
#include <vector>

#include <semaphore.h>

#include "game/Game.h"
#include "host/HostObserver.h"
#include "host/KidSession.h"
#include "ipc/Timeouts.h"

namespace wolf {

struct HostOptions {
    int kids = 3;
    std::chrono::milliseconds turn = kDefaultTurn;
    std::optional<int> fixedWolf;
    std::optional<unsigned> seed;
};

// Хост-волк. Потоки: сигналов, сбора завершившихся потомков, игры
// и по одному потоку KidSession на каждого козлёнка. Локальные семафоры
// лежат в общей памяти, созданной до первого fork.
class HostServer {
public:
    HostServer(HostOptions options, HostObserver& observer);
    ~HostServer();

    HostServer(const HostServer&) = delete;
    HostServer& operator=(const HostServer&) = delete;

    void start();
    void stop();

    void addKid();
    void submitWolfNumber(int number);

private:
    int spawnKid(bool deferStart);
    void childMain(const ConnId& id);
    void signalLoop();
    void reapLoop();
    void gameLoop();

    void onChildExit(pid_t pid, int status);
    void handleReply(int kidId, const proto::Message& reply);
    void handleFailure(int kidId, const std::string& reason);

    void dropKidLocked(int kidId, bool killChild = true);
    std::vector<KidInfo> kidsLocked() const;
    void reapFinishedLocked(std::unique_lock<std::mutex>& lock);

    void notifyKids();
    void status(const std::string& text);

    HostOptions options_;
    HostObserver& observer_;
    const pid_t hostPid_;

    mutable std::mutex mutex_;
    std::mutex spawnMutex_;
    std::condition_variable cv_;
    std::map<int, std::unique_ptr<KidSession>> sessions_;
    std::vector<std::unique_ptr<KidSession>> finished_;
    std::map<pid_t, int> idByPid_;
    std::map<int, KidInfo> info_;
    std::set<pid_t> children_;
    game::Game game_;
    int nextId_ = 1;

    unsigned collectingRound_ = 0;
    std::set<int> waiting_;
    std::map<int, int> throws_;

    std::optional<int> wolfInput_;
    std::mt19937 rng_;
    sem_t* sems_ = nullptr;

    std::atomic<bool> stopping_{false};
    bool started_ = false;
    std::thread signalThread_;
    std::thread reapThread_;
    std::thread gameThread_;
};

}
