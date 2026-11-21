#include "host/Server.hpp"

#include "conn/conn.hpp"
#include "link/Link.hpp"
#include "link/Timeouts.hpp"
#include "util/Journal.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include <signal.h>
#include <unistd.h>

namespace chatlab {

namespace {

using Steady = std::chrono::steady_clock;

constexpr auto kReceiveSlice = std::chrono::milliseconds(250);
constexpr auto kPaceStep = std::chrono::milliseconds(100);
constexpr auto kFarewellGrace = std::chrono::milliseconds(1200);

std::string clientTitle(int id, const std::string& name) {
    return "Клиент " + std::to_string(id) + (name.empty() ? "" : " «" + name + "»");
}

std::string targetTitle(const Post& post) {
    if (!post.isPrivate()) {
        return "всем";
    }
    return "участнику " + std::to_string(post.to);
}

long seconds(std::chrono::milliseconds span) {
    return static_cast<long>(span.count() / 1000);
}

}

struct Server::Session {
    Session(int id, pid_t pid, std::unique_ptr<Link> link) : id(id), pid(pid), link(std::move(link)) {}

    const int id;
    const pid_t pid;
    std::unique_ptr<Link> link;
    std::string name;
    std::uint64_t rosterSent = 0;
    std::thread worker;
    std::atomic<bool> done{false};
    std::atomic<bool> evicted{false};
    std::string evictReason;
};

Server::Server(HostEvents& events, ServerOptions options)
    : events_(events),
      options_(options),
      watcher_([this](pid_t guest) { admit(guest); }, [this](int) { events_.onQuitRequest(); }) {}

Server::~Server() {
    stop();
}

void Server::status(const std::string& text) {
    journal::info(text);
    events_.onStatus(text);
}

void Server::start() {
    const int swept = WorkDir::sweepLeftovers();
    if (swept > 0) {
        journal::info("удалены остатки аварийно завершённых хостов: " + std::to_string(swept));
    }
    workDir_ = std::make_unique<WorkDir>(::getpid());
    running_ = true;
    watcher_.start();
    pacer_ = std::thread(&Server::pace, this);
    status("Хост запущен: pid " + std::to_string(::getpid()) + ", тип связи " + Conn::Kind() +
           ", ожидание клиентов");
    events_.onRoster(hub_.roster());
}

void Server::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    closing_ = true;
    watcher_.stop();

    std::map<int, std::unique_ptr<Session>> sessions;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions.swap(sessions_);
    }
    for (auto& entry : sessions) {
        if (entry.second->worker.joinable()) {
            entry.second->worker.join();
        }
    }
    sessions.clear();
    if (pacer_.joinable()) {
        pacer_.join();
    }
    workDir_.reset();
    journal::info("хост остановлен, каналы и семафоры удалены");
}

void Server::say(int to, const std::string& text) {
    const Accepted accepted = hub_.submit(kHostId, {to, wallMs(), text}, wallMs());
    if (!accepted.ok) {
        status("Сообщение хоста не отправлено: " + accepted.error);
        return;
    }
    journal::info("сообщение #" + std::to_string(accepted.post.seq) + " от хоста " +
                  targetTitle(accepted.post));
}

void Server::admit(pid_t guest) {
    if (closing_) {
        return;
    }
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    reapLocked();
    for (const auto& [id, session] : sessions_) {
        if (session->pid == guest && !session->done) {
            sigctl::answer(guest, id);
            return;
        }
    }
    if (!hub_.hasRoom() || sessions_.size() >= kMemberLimit) {
        status("Процесс " + std::to_string(guest) + " не принят: чат заполнен");
        return;
    }

    const int id = nextId_++;
    std::unique_ptr<Link> link;
    try {
        link = std::make_unique<Link>(ConnKey{::getpid(), id}, Link::Side::Host);
    } catch (const std::exception& error) {
        journal::fail("не удалось создать канал для процесса " + std::to_string(guest) + ": " +
                      error.what());
        events_.onStatus("Ошибка создания канала для процесса " + std::to_string(guest) + ": " +
                         error.what());
        return;
    }
    if (!sigctl::answer(guest, id)) {
        journal::warn("процесс " + std::to_string(guest) + " завершился до ответа на знакомство");
        return;
    }
    status("Знакомство с процессом " + std::to_string(guest) + ": выдан номер " + std::to_string(id));
    auto session = std::make_unique<Session>(id, guest, std::move(link));
    Session& started = *session;
    sessions_[id] = std::move(session);
    started.worker = std::thread(&Server::serve, this, std::ref(started));
}

void Server::serve(Session& session) {
    std::string failure;
    std::optional<packets::Request> request = awaitRequest(session, failure);
    if (!request || request->ask != packets::Ask::Hello) {
        finish(session, request ? "первым пришло не приветствие" : failure);
        return;
    }

    session.name = hub_.join(session.id, session.pid, request->name, wallMs());
    status(clientTitle(session.id, session.name) + " присоединился (pid " +
           std::to_string(session.pid) + ")");
    events_.onRoster(hub_.roster());

    packets::Reply welcome;
    welcome.answer = packets::Answer::Welcome;
    if (!answer(session, std::move(welcome))) {
        finish(session, "не удалось отправить приветствие");
        return;
    }

    while (true) {
        request = awaitRequest(session, failure);
        if (!request) {
            finish(session, failure);
            return;
        }
        accept(session, request->drafts);
        if (request->ask == packets::Ask::Bye) {
            packets::Reply bye;
            bye.answer = packets::Answer::Closed;
            bye.reason = "вы вышли из чата";
            answer(session, std::move(bye));
            finish(session, "вышел из чата");
            return;
        }
        if (closing_) {
            packets::Reply bye;
            bye.answer = packets::Answer::Closed;
            bye.reason = "хост завершает работу";
            answer(session, std::move(bye));
            finish(session, "хост завершает работу");
            return;
        }
        if (!answer(session, packets::Reply{})) {
            finish(session, "канал закрыт");
            return;
        }
    }
}

std::optional<packets::Request> Server::awaitRequest(Session& session, std::string& failure) {
    const auto deadline = Steady::now() + kWaitLimit;
    std::optional<Steady::time_point> farewellDeadline;
    wire::Bytes frame;
    while (true) {
        if (session.evicted) {
            failure = session.evictReason;
            return std::nullopt;
        }
        const LinkStatus result = session.link->receive(frame, kReceiveSlice);
        if (result == LinkStatus::Ok) {
            try {
                return packets::unpackRequest(frame);
            } catch (const wire::Error& error) {
                journal::warn(clientTitle(session.id, session.name) + " прислал повреждённый пакет: " +
                              error.what());
                failure = "повреждённый пакет";
                return std::nullopt;
            }
        }
        if (result != LinkStatus::TimedOut) {
            failure = statusText(result);
            return std::nullopt;
        }
        const auto now = Steady::now();
        if (closing_) {
            if (!farewellDeadline) {
                farewellDeadline = now + kFarewellGrace;
            }
            if (now >= *farewellDeadline) {
                failure = "хост завершает работу";
                return std::nullopt;
            }
        }
        if (now >= deadline) {
            failure = "не отвечает дольше " + std::to_string(seconds(kWaitLimit)) + " с";
            return std::nullopt;
        }
    }
}

bool Server::answer(Session& session, packets::Reply reply) {
    reply.you = session.id;
    reply.yourName = session.name;
    const std::uint64_t stamp = hub_.rosterStamp();
    if (stamp != session.rosterSent) {
        reply.hasRoster = true;
        reply.roster = hub_.roster();
        session.rosterSent = stamp;
    }
    Batch batch = hub_.take(session.id);
    reply.posts = std::move(batch.posts);
    reply.notes = std::move(batch.notes);
    return session.link->send(packets::pack(reply)) == LinkStatus::Ok;
}

void Server::accept(Session& session, const std::vector<Draft>& drafts) {
    for (const Draft& draft : drafts) {
        const Accepted accepted = hub_.submit(session.id, draft, wallMs());
        if (!accepted.ok) {
            hub_.notify(session.id, "Сообщение не отправлено: " + accepted.error);
            continue;
        }
        journal::info("сообщение #" + std::to_string(accepted.post.seq) + " от клиента " +
                      std::to_string(session.id) + " " + targetTitle(accepted.post));
    }
}

void Server::finish(Session& session, const std::string& reason) {
    const bool joined = hub_.isMember(session.id);
    hub_.leave(session.id, reason);
    status(clientTitle(session.id, session.name) + " отключён: " + reason);
    if (joined) {
        events_.onRoster(hub_.roster());
    }
    session.link.reset();
    session.done = true;
}

void Server::evictSilent() {
    const auto silent = hub_.silentFor(wallMs(), options_.silence);
    if (silent.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (const Silent& member : silent) {
        const auto found = sessions_.find(member.id);
        if (found == sessions_.end() || found->second->evicted || found->second->done) {
            continue;
        }
        Session& session = *found->second;
        session.evictReason = "молчал дольше " + std::to_string(seconds(options_.silence)) +
                              " с, отправлен SIGKILL";
        session.evicted = true;
        ::kill(session.pid, SIGKILL);
        journal::warn(clientTitle(member.id, member.name) + " (pid " + std::to_string(member.pid) +
                      ") молчит слишком долго: отправлен SIGKILL");
    }
}

void Server::pace() {
    while (running_) {
        const Released released = hub_.release(wallMs());
        for (const Post& post : released.forHost) {
            events_.onPost(post);
        }
        for (const Post& post : released.relayed) {
            events_.onStatus("Передано личное сообщение: " + post.fromName + " -> " + post.toName);
        }
        evictSilent();
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            reapLocked();
        }
        std::this_thread::sleep_for(kPaceStep);
    }
}

void Server::reapLocked() {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second->done) {
            if (it->second->worker.joinable()) {
                it->second->worker.join();
            }
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

}
