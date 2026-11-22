// Игровая логика хоста (волк): приём козлят, раунды, подсчёт результатов. Не зависит от Qt:
// вывод идёт через интерфейс IGameView (его реализуют консольный режим и окно Qt).
#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "game_rules.h"

struct KidInfo {
    int id;
    int pid;
    bool connected;
    bool alive;
    int lastNumber;
};

struct KidResult {
    int id;
    int pid;
    int number;
    bool wasAlive;
    rules::Outcome outcome;
};

struct RoundReport {
    int round = 0;
    int wolf = 0;
    int kids = 0;  // n
    std::vector<KidResult> results;
    int hidden = 0;   // спрятались
    int caught = 0;   // попались
    int dead = 0;     // мертвы после раунда (всего)
    int revived = 0;  // воскресли
};

std::string FormatRound(const RoundReport& r);

// Интерфейс "отображения". Методы вызываются из разных потоков хоста - реализации обязаны быть потокобезопасными.
class IGameView {
public:
    virtual ~IGameView() = default;
    virtual void OnLog(const std::string& msg) = 0;                     // статусное сообщение
    virtual void OnKids(const std::vector<KidInfo>& kids) = 0;          // список козлят изменился
    virtual void OnWolfPrompt(bool active, int timeoutMs) = 0;          // окно ввода числа волка открыто/закрыто
    virtual void OnRound(const RoundReport& report) = 0;                // итоги раунда
    virtual void OnGameOver(const std::string& reason) = 0;
};

struct GameConfig {
    int wolfTimeoutMs = 3000;   // сколько ждать число волка от пользователя
    int roundDelayMs = 1500;    // пауза между раундами
    int fixedWolf = 0;          // >0 - волк всегда бросает это число (для тестов)
    bool interactive = false;   // true - число волка вводит пользователь (GUI), иначе случайно
    int waitKids = 1;           // сколько козлят дождаться перед первым раундом
    int maxRounds = 0;          // >0 - ограничить число раундов (для тестов/демонстрации)
};

class GameHost {
public:
    GameHost(IGameView& view, const GameConfig& cfg);
    ~GameHost();
    GameHost(const GameHost&) = delete;
    GameHost& operator=(const GameHost&) = delete;

    void Start();  // запустить поток приёма клиентов (handshake) и поток игры
    void Stop();   // корректно остановить всё (сообщает клиентам о завершении)

    // Число волка от пользователя. false, если сейчас число не ожидается.
    bool SubmitWolfNumber(int number);

    bool Finished() const { return finished_; }

private:
    struct Kid;

    void AcceptLoop();
    void HandleJoinRequest(pid_t pid);
    void GameLoop();
    void WorkerMain(Kid* kid);
    bool DoRound(Kid* kid, int wolf, int kids);
    bool IdleTick(Kid* kid);
    bool FailKid(Kid* kid, const std::string& why);
    void WorkerExit(Kid* kid);
    int ObtainWolfNumber();
    void SleepInterruptible(int ms);
    std::vector<Kid*> ReadyKids();
    std::vector<KidInfo> Snapshot();
    RoundReport BuildReport(int round, int wolf, const std::vector<Kid*>& kids);
    void FinishGame(const std::string& reason);
    void PushKids();
    int RandomInt(int lo, int hi);

    IGameView& view_;
    GameConfig cfg_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> finished_{false};
    std::thread acceptThread_, gameThread_;

    std::mutex kidsMutex_;
    std::vector<std::unique_ptr<Kid>> kids_;
    int nextId_ = 1;

    std::mutex gm_;  // барьер раунда
    std::condition_variable gcv_;
    int pending_ = 0;

    std::mutex wm_;  // ввод числа волка
    std::condition_variable wcv_;
    bool wolfPrompt_ = false;
    bool wolfSubmitted_ = false;
    int wolfValue_ = 0;

    std::mutex sleepMutex_;
    std::condition_variable sleepCv_;

    std::mt19937 rng_;
};
