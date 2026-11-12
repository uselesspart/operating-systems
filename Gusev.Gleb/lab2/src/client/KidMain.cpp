#include "client/KidMain.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>

#include "conn/conn.h"
#include "game/Game.h"
#include "ipc/LocalSemaphore.h"
#include "ipc/Protocol.h"
#include "ipc/Timeouts.h"
#include "util/Log.h"

namespace wolf {

namespace {

void say(const std::string& text) {
    std::printf("%s\n", text.c_str());
    std::fflush(stdout);
}

}

int runKid(const KidOptions& options, Conn& conn, sem_t* toKid, sem_t* toHost) {
    const std::string type = Conn::typeCode();
    log::info("козлёнок #" + std::to_string(options.id) + " запущен (pid " +
              std::to_string(::getpid()) + ", канал " + type + ")");
    say("Козлёнок #" + std::to_string(options.id) + " в игре (волк — pid " +
        std::to_string(::getppid()) + ", канал " + type + ")");

    std::mt19937 rng(options.seed);

    for (;;) {
        if (!sem::wait(toKid, kIpcTimeout)) {
            log::error("волк молчит дольше 5 с — связь прервана");
            return 1;
        }

        proto::Message message;
        if (!conn.Read(&message, sizeof message)) {
            log::error("канал закрыт — связь прервана");
            return 1;
        }
        if (!proto::isValid(message)) {
            log::error("получено некорректное сообщение — связь прервана");
            return 1;
        }

        switch (message.type) {
            case proto::Type::Round: {
                const bool alive = message.alive != 0;
                const int max = game::maxThrow(alive);
                const int number = std::uniform_int_distribution<int>(1, max)(rng);
                say("Раунд " + std::to_string(message.round) + ": я " +
                    (alive ? "жив" : "мёртв") + ", козлят " + std::to_string(message.kids) +
                    ", бросаю " + std::to_string(number) + " (1.." + std::to_string(max) + ")");
                log::info("бросок в раунде " + std::to_string(message.round) + ": бросаю " +
                          std::to_string(number));
                const proto::Message reply = proto::makeThrow(message.round, number);
                if (!conn.Write(&reply, sizeof reply)) {
                    log::error("не удалось отправить бросок — связь прервана");
                    return 1;
                }
                sem::post(toHost);
                break;
            }
            case proto::Type::Result:
                say("  волк выбросил " + std::to_string(message.wolf) + ", разница " +
                    std::to_string(std::abs(message.number - message.wolf)) + " → " +
                    proto::outcomeName(message.outcome) + " (теперь " +
                    (message.alive ? "жив" : "мёртв") + "). Итоги: спрятались " +
                    std::to_string(message.hidden) + ", попались " + std::to_string(message.caught) +
                    ", мертвы " + std::to_string(message.dead) + " из " +
                    std::to_string(message.kids));
                log::info("итог раунда " + std::to_string(message.round) + ": волк " +
                          std::to_string(message.wolf) + ", мой бросок " +
                          std::to_string(message.number) + ", " +
                          proto::outcomeName(message.outcome));
                break;
            case proto::Type::GameOver:
                say("Игра окончена на раунде " + std::to_string(message.round) +
                    ": все козлята мертвы два хода подряд. Волк победил.");
                log::info("игра окончена, выхожу");
                return 0;
            case proto::Type::Bye:
                say("Волк завершил работу. До свидания!");
                log::info("хост попрощался, выхожу");
                return 0;
            case proto::Type::Throw:
                log::error("неожиданное сообщение THROW от хоста");
                return 1;
        }
    }
}

}
