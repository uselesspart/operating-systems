#pragma once

#include "connection.hpp"
#include "game.hpp"
#include "logger.hpp"
#include "options.hpp"
#include <csignal>
#include <random>
#include <sys/types.h>

namespace lab {
extern volatile std::sig_atomic_t interrupted;
int runClient(Connection& connection, const Options& options, const Logger& logger);
class Session {
public:
    explicit Session(const Options& options);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    void waitReady();
    int nextWolf();
    Round play(int wolf);
    void stop() noexcept;
    bool limited() const;
    bool finished() const { return game_.finished() || limited(); }
    const Game& game() const { return game_; }
    const Options& options() const { return options_; }
    const Logger& logger() const { return logger_; }
    pid_t childPid() const { return child_; }
private:
    Options options_;
    Logger logger_;
    std::unique_ptr<Connection> connection_;
    pid_t child_ = -1;
    bool connected_ = false;
    bool closing_ = false;
    Game game_;
    std::mt19937 random_;
};
}
