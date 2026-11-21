#include "check.hpp"

#include "client/GuestSession.hpp"
#include "host/Server.hpp"
#include "link/Signals.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

using namespace chatlab;
using namespace std::chrono_literals;

namespace {

class RecordingEvents : public HostEvents {
  public:
    void onStatus(const std::string& text) override {
        std::lock_guard<std::mutex> lock(mutex_);
        statuses_.push_back(text);
        changed_.notify_all();
    }
    void onRoster(const std::vector<Member>& roster) override {
        std::lock_guard<std::mutex> lock(mutex_);
        rosterSize_ = roster.size();
        changed_.notify_all();
    }
    void onPost(const Post& post) override {
        std::lock_guard<std::mutex> lock(mutex_);
        posts_.push_back(post);
        changed_.notify_all();
    }
    void onQuitRequest() override {}

    bool waitStatus(const std::string& part, std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, limit, [&] {
            for (const std::string& text : statuses_) {
                if (unit::contains(text, part)) {
                    return true;
                }
            }
            return false;
        });
    }

    bool waitPost(const std::string& text, std::chrono::milliseconds limit) {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, limit, [&] {
            for (const Post& post : posts_) {
                if (post.text == text) {
                    return true;
                }
            }
            return false;
        });
    }

    std::size_t rosterSize() {
        std::lock_guard<std::mutex> lock(mutex_);
        return rosterSize_;
    }

  private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::vector<std::string> statuses_;
    std::vector<Post> posts_;
    std::size_t rosterSize_ = 0;
};

int childExit(pid_t child) {
    int status = 0;
    ::waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

bool hasPost(const packets::Reply& reply, const std::string& text, int to) {
    for (const Post& post : reply.posts) {
        if (post.text == text && post.to == to) {
            return true;
        }
    }
    return false;
}

int guestScenario(pid_t host) {
    GuestSession session(host, "Тестовый");
    packets::Reply welcome;
    try {
        welcome = session.connect();
    } catch (const GuestError&) {
        return 10;
    }
    if (welcome.yourName != "Тестовый" || !welcome.hasRoster || welcome.roster.size() != 2) {
        return 11;
    }
    const Millis now = wallMs();
    Exchange sent = session.poll({{kEveryone, now, "от гостя всем"}, {kHostId, now, "хосту лично"}});
    if (!sent.ok) {
        return 12;
    }
    bool sawPrivate = false;
    bool sawPublic = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!(sawPrivate && sawPublic) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPollPeriod);
        const Exchange polled = session.poll({});
        if (!polled.ok) {
            return 13;
        }
        sawPrivate = sawPrivate || hasPost(polled.reply, "ответ лично", session.id());
        sawPublic = sawPublic || hasPost(polled.reply, "ответ всем", kEveryone);
    }
    if (!sawPrivate || !sawPublic) {
        return 14;
    }
    const Exchange bye = session.leave({});
    return bye.ok && bye.reply.answer == packets::Answer::Closed ? 0 : 15;
}

}

TEST(server_relays_between_guest_and_host_window) {
    sigctl::blockForThreads();
    RecordingEvents events;
    Server server(events);
    server.start();
    CHECK(events.waitStatus("Хост запущен", 2000ms));

    const pid_t host = ::getpid();
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(guestScenario(host));
    }

    CHECK(events.waitStatus("«Тестовый» присоединился", 6000ms));
    CHECK_EQ(events.rosterSize(), std::size_t(2));
    CHECK(events.waitPost("от гостя всем", 3000ms));
    CHECK(events.waitPost("хосту лично", 3000ms));
    server.say(1, "ответ лично");
    server.say(kEveryone, "ответ всем");
    server.say(42, "некому");
    CHECK(events.waitStatus("Сообщение хоста не отправлено", 1000ms));

    CHECK_EQ(childExit(child), 0);
    CHECK(events.waitStatus("отключён: вышел из чата", 3000ms));
    server.stop();
}

TEST(server_drops_guest_that_never_says_hello) {
    sigctl::blockForThreads();
    RecordingEvents events;
    Server server(events);
    server.start();
    const pid_t host = ::getpid();
    const pid_t child = ::fork();
    if (child == 0) {
        const sigctl::KnockResult knock = sigctl::knock(host);
        if (knock.outcome != sigctl::KnockOutcome::Admitted) {
            ::_exit(1);
        }
        ::sleep(7);
        ::_exit(0);
    }
    CHECK(events.waitStatus("выдан номер", 3000ms));
    CHECK(events.waitStatus("не отвечает дольше 5 с", 8000ms));
    ::kill(child, SIGKILL);
    childExit(child);
    server.stop();
}
