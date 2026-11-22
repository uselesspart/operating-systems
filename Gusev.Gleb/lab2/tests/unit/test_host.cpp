#include <condition_variable>
#include <mutex>

#include "host/HostServer.h"
#include "test.h"

TEST_SUITE("HostServer — запуск без козлят и добавление на ходу");

using namespace std::chrono_literals;

namespace {

struct Observer : wolf::HostObserver {
    std::mutex mutex;
    std::condition_variable cv;
    int kids = 0;

    void onStatus(const std::string&) override {}
    void onKids(const std::vector<wolf::KidInfo>& list) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            kids = static_cast<int>(list.size());
        }
        cv.notify_all();
    }
    void onTurnStarted(int, std::chrono::milliseconds) override {}
    void onRound(const wolf::game::RoundResult&, wolf::WolfSource) override {}
    void onGameOver(int) override {}
    void onTerminateRequested() override {}

    bool waitKids(int expected, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this, expected] { return kids == expected; });
    }
};

}

TEST(add_at_runtime, "хост без козлят принимает новых потомков через addKid") {
    Observer observer;
    wolf::HostOptions options;
    options.kids = 0;
    options.turn = 100ms;
    options.fixedWolf = 50;
    options.seed = 1;

    wolf::HostServer server(options, observer);
    server.start();

    server.addKid();
    EXPECT_TRUE(observer.waitKids(1, 3s));

    server.addKid();
    EXPECT_TRUE(observer.waitKids(2, 3s));

    server.stop();
    EXPECT_TRUE(true);
}
