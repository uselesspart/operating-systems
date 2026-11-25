#include "host.hpp"
#include "Listener.hpp"
#include "ipc.hpp"
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <filesystem>
#include <map>
#include <poll.h>
#include <set>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace application {
namespace {
constexpr auto tick = std::chrono::milliseconds(100);
std::int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
struct Session {
    int pid;
    std::string name;
    std::shared_ptr<IConnection> connection;
    std::atomic<bool> active{true};
    std::atomic<bool> finished{false};
    std::atomic<bool> idle_expired{false};
    std::atomic<std::int64_t> last_sent{steady_ms()};
    Fd process;
    Session(int value, std::shared_ptr<IConnection> channel)
        : pid(value), connection(std::move(channel)),
          process(static_cast<int>(syscall(SYS_pidfd_open, value, 0))) {
        if (process.get() < 0) { system_error("pidfd_open client"); }
    }
    bool alive() const {
        pollfd item{process.get(), POLLIN, 0};
        const int result = poll(&item, 1, 0);
        if (result < 0) { system_error("poll client pidfd"); }
        return result == 0;
    }
    void kill_inactive() const {
        // pidfd addresses this exact process even if its numeric PID is reused.
        if (syscall(SYS_pidfd_send_signal, process.get(), SIGKILL, nullptr, 0) < 0 && errno != ESRCH) {
            system_error("SIGKILL inactive client " + std::to_string(pid));
        }
    }
};
struct Worker { std::shared_ptr<Session> session; std::jthread thread; };
enum class Operation { Join, Chat, Gone };
struct Work {
    Operation operation;
    std::shared_ptr<Session> session;
    Packet packet;
    std::string reason;
};
} // namespace
struct Server::Impl {
    ServerConfig config;
    EventSink sink;
    const int pid = getpid();
    const std::string root = root_path(pid);
    bool owns_root = false;
    bool started = false;
    std::atomic<bool> running{false};
    std::unique_ptr<NamedSemaphore> registration;
    std::unique_ptr<IListener> listener;
    std::jthread signaler;
    std::jthread dispatcher;
    std::jthread watchdog;
    std::vector<Worker> workers;
    std::mutex queue_mutex;
    std::condition_variable queue_ready;
    std::deque<Work> queue;
    std::mutex pids_mutex;
    std::set<int> pending_pids;
    std::mutex watch_mutex;
    std::map<int, std::shared_ptr<Session>> watched;
    // Only the dispatcher accesses the registry and assigns message order.
    std::map<int, std::shared_ptr<Session>> clients;
    std::uint64_t sequence = 0;
    std::int64_t last_time = 0;

    Impl(ServerConfig value, EventSink callback) : config(std::move(value)), sink(std::move(callback)) {}
    ~Impl() {
        registration.reset();
        if (owns_root) {
            std::error_code ignored;
            std::filesystem::remove_all(root, ignored);
        }
    }
    void enqueue(Work work) {
        {
            std::lock_guard lock(queue_mutex);
            if (queue.size() >= 1024) { throw Error("Host message queue is full"); }
            queue.push_back(std::move(work));
        }
        queue_ready.notify_one();
    }
    void stop() noexcept { running = false; queue_ready.notify_all(); }
    bool deliver(const std::shared_ptr<Session>& session, const Packet& packet) {
        if (!session->active) { return false; }
        try { session->connection->send(encode(packet)); return true; }
        catch (const std::exception& error) {
            session->active = false;
            session->connection->close();
            notify(sink, EventKind::Error, "PID " + std::to_string(session->pid) + ": " + error.what());
            return false;
        }
    }
    void broadcast(const Packet& packet) {
        for (const auto& [id, session] : clients) { (void)id; deliver(session, packet); }
    }
    void remove_client(int id, const std::string& reason) {
        const auto found = clients.find(id);
        if (found == clients.end()) { return; }
        auto session = found->second;
        const std::string final_reason = session->idle_expired ? "Inactivity exceeded 60 seconds; SIGKILL" : reason;
        session->active = false;
        session->connection->close();
        clients.erase(found);
        { std::lock_guard lock(watch_mutex); watched.erase(id); }
        Packet packet;
        packet.kind = Kind::Left;
        packet.sender = id;
        packet.name = session->name;
        packet.text = final_reason;
        broadcast(packet);
        notify(sink, EventKind::Left, final_reason, packet);
    }
    void add_client(const std::shared_ptr<Session>& session) {
        bool duplicate = session->name == config.name;
        for (const auto& [id, existing] : clients) {
            (void)id;
            duplicate = duplicate || existing->name == session->name;
        }
        if (duplicate) {
            Packet failure;
            failure.kind = Kind::Failure;
            failure.text = "Name already in use";
            deliver(session, failure);
            session->active = false;
            return;
        }
        Packet welcome;
        welcome.kind = Kind::Welcome;
        welcome.sender = pid;
        welcome.recipient = session->pid;
        welcome.name = config.name;
        if (!deliver(session, welcome)) { return; }
        Packet joined;
        joined.kind = Kind::Joined;
        joined.sender = pid;
        joined.name = config.name;
        deliver(session, joined);
        for (const auto& [id, existing] : clients) {
            joined.sender = id;
            joined.name = existing->name;
            deliver(session, joined);
        }
        if (!session->active) { return; }
        clients.emplace(session->pid, session);
        session->last_sent = steady_ms();
        { std::lock_guard lock(watch_mutex); watched.emplace(session->pid, session); }
        joined.sender = session->pid;
        joined.name = session->name;
        broadcast(joined);
        notify(sink, EventKind::Joined, "Connected", joined);
    }
    void route(Work& work) {
        const int sender = work.session ? work.session->pid : pid;
        if (work.session && (!clients.contains(sender) || !work.session->active)) { return; }
        auto packet = work.packet;
        packet.sender = sender;
        packet.name = work.session ? work.session->name : config.name;
        if (packet.recipient != 0 && packet.recipient != pid && !clients.contains(packet.recipient)) {
            Packet failure;
            failure.kind = Kind::Failure;
            failure.text = "Recipient is not connected: " + std::to_string(packet.recipient);
            if (work.session) { deliver(work.session, failure); }
            else { notify(sink, EventKind::Error, failure.text); }
            return;
        }
        // The host commits a send here: one clock, monotonic timestamps and a
        // sequence shared by every participant. Client clocks are not trusted.
        packet.sequence = ++sequence;
        packet.time_ms = std::max(now_ms(), last_time + 1);
        last_time = packet.time_ms;
        if (packet.recipient == 0) { broadcast(packet); }
        else {
            if (work.session) { deliver(work.session, packet); }
            if (packet.recipient != sender && packet.recipient != pid) {
                deliver(clients.at(packet.recipient), packet);
            }
        }
        if (packet.recipient == 0 || sender == pid || packet.recipient == pid) {
            notify(sink, EventKind::Message, {}, packet);
        }
    }
    void dispatch_loop() {
        try {
            while (running) {
                std::deque<Work> batch;
                {
                    std::unique_lock lock(queue_mutex);
                    queue_ready.wait_for(lock, tick, [this] { return !queue.empty() || !running; });
                    batch.swap(queue);
                }
                for (auto& work : batch) {
                    if (!running) { break; }
                    switch (work.operation) {
                    case Operation::Join: add_client(work.session); break;
                    case Operation::Chat: route(work); break;
                    case Operation::Gone: remove_client(work.session->pid, work.reason); break;
                    }
                }
                std::vector<std::pair<int, std::string>> expired;
                for (const auto& [id, session] : clients) {
                    if (!session->active || !session->alive()) {
                        expired.emplace_back(id, "Disconnected");
                    }
                }
                for (const auto& [id, reason] : expired) { remove_client(id, reason); }
            }
        } catch (const std::exception& error) {
            notify(sink, EventKind::Error, error.what());
            stop();
        }
        Packet shutdown;
        shutdown.kind = Kind::Shutdown;
        shutdown.text = "Host stopped";
        broadcast(shutdown);
        for (auto& [id, session] : clients) {
            (void)id;
            session->active = false;
            session->connection->close();
        }
        clients.clear();
        { std::lock_guard lock(watch_mutex); watched.clear(); }
        notify(sink, EventKind::Stopped, "Host stopped");
    }
    void watch_loop() {
        try {
            while (running) {
                std::vector<std::shared_ptr<Session>> snapshot;
                {
                    std::lock_guard lock(watch_mutex);
                    for (const auto& [id, session] : watched) { (void)id; snapshot.push_back(session); }
                }
                for (const auto& session : snapshot) {
                    if (session->active && steady_ms() - session->last_sent.load() > 60000) {
                        session->idle_expired = true;
                        session->kill_inactive();
                        session->active = false;
                        enqueue({Operation::Gone, session, {}, "Inactivity exceeded 60 seconds; SIGKILL"});
                    }
                }
                std::this_thread::sleep_for(tick);
            }
        } catch (const std::exception& error) { notify(sink, EventKind::Error, error.what()); stop(); }
    }
    void worker(const std::shared_ptr<Session>& session) {
        std::string reason = "Disconnected";
        try {
            const auto deadline = Clock::now() + ipc_timeout;
            std::optional<Bytes> bytes;
            while (running && Clock::now() < deadline && !bytes) {
                bytes = session->connection->receive(tick);
            }
            if (!bytes) { throw Error("HELLO timed out after 5 seconds"); }
            const auto hello = decode(*bytes);
            if (hello.kind != Kind::Hello || hello.sender != session->pid) { throw Error("Invalid HELLO identity"); }
            validate_name(hello.name);
            session->name = hello.name;
            enqueue({Operation::Join, session, {}, {}});
            while (running && session->active) {
                if (!session->alive()) { break; }
                const auto data = session->connection->receive(tick);
                if (!data) { continue; }
                auto packet = decode(*data);
                if (packet.sender != session->pid) { throw Error("Sender PID mismatch"); }
                if (packet.kind == Kind::Leave) { break; }
                if (packet.kind != Kind::Chat) { throw Error("Unexpected client packet"); }
                validate_text(packet.text);
                session->last_sent = steady_ms();
                enqueue({Operation::Chat, session, std::move(packet), {}});
            }
        } catch (const std::exception& error) {
            reason = error.what();
            if (running && session->active) {
                notify(sink, EventKind::Error, "PID " + std::to_string(session->pid) + ": " + reason);
            }
        }
        if (running) {
            try { enqueue({Operation::Gone, session, {}, reason}); }
            catch (const std::exception& error) { notify(sink, EventKind::Error, error.what()); stop(); }
        }
        std::lock_guard lock(pids_mutex);
        pending_pids.erase(session->pid);
        session->finished = true;
    }
    void signal_loop() {
        try {
            while (running) {
                for (auto worker = workers.begin(); worker != workers.end();) {
                    if (worker->session->finished) {
                        worker->thread.join();
                        worker = workers.erase(worker);
                    } else { ++worker; }
                }
                const auto [signal, sender] = wait_signal(true, tick);
                if (signal == SIGINT || signal == SIGTERM) { stop(); break; }
                if (signal != SIGUSR1 || sender <= 1 || sender == pid) { continue; }
                {
                    std::lock_guard lock(pids_mutex);
                    if (!pending_pids.insert(sender).second) { continue; }
                }
                try {
                    auto pending = listener->prepare(sender);
                    if (kill(sender, SIGUSR2) < 0) { system_error("SIGUSR2"); }
                    auto session = std::make_shared<Session>(sender, pending->accept());
                    workers.push_back({session, std::jthread([this, session] { worker(session); })});
                } catch (const std::exception& error) {
                    notify(sink, EventKind::Error, "Handshake PID " + std::to_string(sender) + ": " + error.what());
                    std::lock_guard lock(pids_mutex);
                    pending_pids.erase(sender);
                }
            }
        } catch (const std::exception& error) { notify(sink, EventKind::Error, error.what()); stop(); }
    }
};
Server::Server(ServerConfig config, EventSink sink)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(sink))) {}
Server::~Server() { request_stop(); join(); }
void Server::start() {
    auto& app = *impl_;
    if (app.started) { throw Error("Host cannot be started twice"); }
    validate_name(app.config.name);
    make_directory(app.root);
    app.owns_root = true;
    app.registration = std::make_unique<NamedSemaphore>(
        "/pn" + std::string(TYPE_CODE) + "_" + std::to_string(app.pid) + "_join", true, 1);
    app.listener = make_listener(app.root);
    app.started = true;
    app.running = true;
    app.dispatcher = std::jthread([&app] { app.dispatch_loop(); });
    app.watchdog = std::jthread([&app] { app.watch_loop(); });
    app.signaler = std::jthread([&app] { app.signal_loop(); });
    Packet self;
    self.kind = Kind::Joined;
    self.sender = app.pid;
    self.name = app.config.name;
    notify(app.sink, EventKind::Joined, "Host", self);
    notify(app.sink, EventKind::Status, "READY host=" + std::to_string(app.pid) + " transport=" + TYPE_CODE);
}
void Server::post_message(const std::string& text, int recipient) {
    if (!running()) { throw Error("Host is not running"); }
    validate_text(text);
    if (recipient < 0) { throw Error("Invalid recipient PID"); }
    Packet packet;
    packet.text = text;
    packet.recipient = recipient;
    impl_->enqueue({Operation::Chat, {}, std::move(packet), {}});
}
void Server::request_stop() noexcept { impl_->stop(); }
void Server::join() {
    auto& app = *impl_;
    if (app.signaler.joinable()) { app.signaler.join(); }
    if (app.watchdog.joinable()) { app.watchdog.join(); }
    for (auto& worker : app.workers) { if (worker.thread.joinable()) { worker.thread.join(); } }
    if (app.dispatcher.joinable()) { app.dispatcher.join(); }
    app.workers.clear();
    { std::lock_guard lock(app.queue_mutex); app.queue.clear(); }
}
bool Server::running() const noexcept { return impl_->running; }
} // namespace application
