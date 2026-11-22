#pragma once

#include <sys/types.h>

#include <chrono>
#include <string>
#include <vector>

#include "game/Game.h"

namespace wolf {

struct KidInfo {
    int id = 0;
    pid_t pid = 0;
    bool alive = true;
    int lastNumber = 0;
    proto::Outcome lastOutcome = proto::Outcome::None;
};

enum class WolfSource { User, Fixed, Random };

inline const char* wolfSourceName(WolfSource s) {
    switch (s) {
        case WolfSource::User:  return "введено";
        case WolfSource::Fixed: return "задано --wolf";
        case WolfSource::Random: return "случайно";
    }
    return "?";
}

// Интерфейс окна. Методы вызываются из потоков сервера, поэтому реализация
// сама перекладывает работу в поток Qt.
class HostObserver {
public:
    virtual ~HostObserver() = default;
    virtual void onStatus(const std::string& text) = 0;
    virtual void onKids(const std::vector<KidInfo>& kids) = 0;
    virtual void onTurnStarted(int round, std::chrono::milliseconds window) = 0;
    virtual void onRound(const game::RoundResult& result, WolfSource source) = 0;
    virtual void onGameOver(int rounds) = 0;
    virtual void onTerminateRequested() = 0;
};

}
