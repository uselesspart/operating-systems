#pragma once

#include <sys/types.h>

#include <atomic>
#include <functional>
#include <string>

#include "common.h"
#include "game.h"

struct WolfChoice {
    int number;
    bool manual;
};

class WolfSession {
public:
    struct Callbacks {
        std::function<WolfChoice(int round)> wolfNumber;
        std::function<void(const std::string&)> status;
        std::function<void(const RoundResult&)> roundDone;
    };

    WolfSession(Shared* shared, pid_t goat, int id, Callbacks callbacks);

    int run(int maxRounds, const std::atomic<bool>& stop);

private:
    void status(const std::string& text);
    bool endGame();
    void reapGoat(bool force);

    Shared* shared_;
    pid_t goat_;
    int id_;
    Callbacks cb_;
    class Conn* conn_ = nullptr;
};
