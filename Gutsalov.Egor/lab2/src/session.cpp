#include "chat.hpp"
#include "connection.hpp"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <deque>
#include <filesystem>
#include <map>
#include <poll.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace chat {
void block_signals() {
    sigset_t mask;
    sigemptyset(&mask);
    for (int signal : {SIGUSR1, SIGUSR2, SIGTERM, SIGINT, SIGPIPE}) sigaddset(&mask, signal);
    require(pthread_sigmask(SIG_BLOCK, &mask, nullptr) == 0, "pthread_sigmask");
}
static sigset_t signal_set(std::initializer_list<int> signals) {
    sigset_t mask;
    sigemptyset(&mask);
    for (int signal : signals) sigaddset(&mask, signal);
    return mask;
}
static void acknowledge(int pid) {
    union sigval value{};
    value.sival_int = pid;
    require(sigqueue(pid, SIGUSR2, value) == 0, "sigqueue(ack)");
}
struct Server::Impl {
    struct Peer {
        explicit Peer(int value) : pid(value), pidfd(static_cast<int>(syscall(SYS_pidfd_open, value, 0))) {
            require(pidfd.get() >= 0, "pidfd_open");
        }
        int pid;
        Fd pidfd;
        std::thread thread;
        std::atomic<bool> stop{false}, done{false}, prepared{false}, connected{false};
        Clock::time_point activity = Clock::now();
        std::deque<Message> outgoing; // guarded by Impl::mutex
    };
    struct Pending { Message message; Clock::time_point arrival; };
    Events& events;
    std::atomic<bool> stop{false};
    std::thread dispatcher;
    std::mutex mutex;
    std::map<int, std::shared_ptr<Peer>> peers;
    std::vector<Pending> pending;
    std::uint64_t sequence = 0;
    std::int64_t last_timestamp = 0;
    std::string directory = session_path(getpid());

    explicit Impl(Events& output) : events(output) {
        require(mkdir(directory.c_str(), 0700) == 0, "mkdir(session)");
        try { dispatcher = std::thread([this] { run(); }); }
        catch (...) { rmdir(directory.c_str()); throw; }
    }
    ~Impl() {
        stop = true;
        if (dispatcher.joinable()) dispatcher.join();
        rmdir(directory.c_str());
    }
    void enqueue(int sender, Message message) {
        message.sender = sender;
        message.sequence = 0;
        validate(message);
        const auto now = now_ns();
        if (message.timestamp < now - 5'000'000'000LL || message.timestamp > now + 1'000'000'000LL)
            throw std::runtime_error("Message timestamp outside allowed window");
        std::lock_guard lock(mutex);
        if (pending.size() >= 4096) throw std::runtime_error("Host queue is full");
        if (sender != 0) {
            auto it = peers.find(sender);
            if (it == peers.end()) throw std::runtime_error("Client disconnected");
            it->second->activity = Clock::now();
        }
        pending.push_back({std::move(message), Clock::now()});
    }
    // sequence == 0 is reserved for server status notifications on the wire.
    void notify_locked(const std::string& text, int recipient = -1) {
        if (recipient <= 0) events.push("STATUS " + text);
        for (auto& [pid, peer] : peers) {
            if (!peer->connected || peer->done || (recipient >= 0 && recipient != pid)) continue;
            if (peer->outgoing.size() >= 256) peer->stop = true;
            else peer->outgoing.push_back({0, now_ns(), 0, recipient, text});
        }
    }
    void worker(const std::shared_ptr<Peer>& peer) {
        try {
            Channel channel(getpid(), peer->pid, true);
            channel.prepare();
            peer->prepared = true;
            acknowledge(peer->pid);
            channel.open();
            {
                std::lock_guard lock(mutex);
                peer->connected = true;
                notify_locked("connected " + std::to_string(peer->pid));
                std::string participants = "participants 0";
                for (const auto& [pid, connected] : peers) {
                    if (!connected->connected) continue;
                    if (participants.size() > 800) { notify_locked(participants, peer->pid); participants = "participants"; }
                    participants += " " + std::to_string(pid);
                }
                notify_locked(participants, peer->pid);
            }
            while (!stop && !peer->stop) {
                std::deque<Message> outgoing;
                {
                    std::lock_guard lock(mutex);
                    outgoing.swap(peer->outgoing);
                }
                for (const auto& message : outgoing) channel.write(message);
                Message incoming;
                if (channel.read(incoming, peer->stop)) enqueue(peer->pid, std::move(incoming));
            }
        } catch (const std::exception& error) {
            if (!stop && !peer->stop) events.push("STATUS disconnected " + std::to_string(peer->pid) + " " + error.what());
        }
        peer->connected = false;
        peer->done = true;
    }
    void attach(int pid) {
        if (pid <= 1 || pid == getpid()) return;
        auto existing = peers.find(pid);
        if (existing != peers.end()) {
            if (existing->second->prepared && !existing->second->done) acknowledge(pid);
            return;
        }
        auto peer = std::make_shared<Peer>(pid);
        {
            std::lock_guard lock(mutex);
            peers.emplace(pid, peer);
        }
        try { peer->thread = std::thread([this, peer] { worker(peer); }); }
        catch (...) { std::lock_guard lock(mutex); peers.erase(pid); throw; }
    }
    void dispatch() {
        std::lock_guard lock(mutex);
        const auto now = Clock::now();
        std::stable_sort(pending.begin(), pending.end(), [](const Pending& left, const Pending& right) {
            return left.message.timestamp < right.message.timestamp;
        });
        while (!pending.empty() && now - pending.front().arrival >= std::chrono::milliseconds(150)) {
            auto message = std::move(pending.front().message);
            pending.erase(pending.begin());
            if (message.timestamp < last_timestamp) {
                notify_locked("rejected late message", message.sender);
                continue;
            }
            if (message.recipient > 0 && (!peers.contains(message.recipient) || !peers.at(message.recipient)->connected)) {
                notify_locked("unknown recipient " + std::to_string(message.recipient), message.sender);
                continue;
            }
            last_timestamp = message.timestamp;
            message.sequence = ++sequence;
            if (visible_to(message, 0)) events.push(display(message));
            for (auto& [pid, peer] : peers) {
                if (!peer->connected || peer->done || !visible_to(message, pid)) continue;
                if (peer->outgoing.size() >= 256) {
                    peer->stop = true;
                    events.push("STATUS disconnected slow client " + std::to_string(pid));
                } else peer->outgoing.push_back(message);
            }
        }
        for (auto& [pid, peer] : peers) {
            if (!peer->done && !peer->stop && now - peer->activity >= std::chrono::seconds(60)) {
                peer->stop = true;
                const int result = static_cast<int>(syscall(SYS_pidfd_send_signal, peer->pidfd.get(), SIGKILL, nullptr, 0));
                events.push("STATUS idle-kill " + std::to_string(pid) + (result == 0 ? "" : " failed"));
            }
        }
    }
    void reap() {
        for (auto it = peers.begin(); it != peers.end();) {
            if (!it->second->done) { ++it; continue; }
            it->second->thread.join();
            std::lock_guard lock(mutex);
            notify_locked("left " + std::to_string(it->first));
            it = peers.erase(it);
        }
    }
    void run() {
        try {
            const auto mask = signal_set({SIGUSR1, SIGTERM, SIGINT});
            Fd signals(signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC));
            require(signals.get() >= 0, "signalfd");
            events.push("READY host " + std::to_string(getpid()) + " " + transport_name());
            while (!stop) {
                pollfd descriptor{signals.get(), POLLIN, 0};
                const int result = poll(&descriptor, 1, 20);
                if (result < 0 && errno != EINTR) require(false, "poll(signals)");
                signalfd_siginfo info{};
                while (::read(signals.get(), &info, sizeof(info)) == sizeof(info)) {
                    if (info.ssi_signo == SIGTERM || info.ssi_signo == SIGINT) { stop = true; break; }
                    if (info.ssi_uid == geteuid()) {
                        try { attach(static_cast<int>(info.ssi_pid)); }
                        catch (const std::exception& error) { events.push(std::string("STATUS handshake error ") + error.what()); }
                    }
                }
                dispatch();
                reap();
            }
        } catch (const std::exception& error) { events.push(std::string("ERROR host ") + error.what()); stop = true; }
        for (auto& [pid, peer] : peers) { (void)pid; peer->stop = true; }
        for (auto& [pid, peer] : peers) { (void)pid; peer->thread.join(); }
        {
            std::lock_guard lock(mutex);
            peers.clear();
        }
        events.push("STATUS host stopped");
    }
};
Server::Server(Events& events) : impl_(std::make_unique<Impl>(events)) {}
Server::~Server() = default;
void Server::send(int recipient, const std::string& text) { impl_->enqueue(0, {0, now_ns(), 0, recipient, text}); }
bool Server::alive() const { return !impl_->stop; }

struct Client::Impl {
    Events& events;
    Channel channel;
    std::atomic<bool> stop{false};
    std::thread reader;
    Impl(int host, Events& output) : events(output), channel(host, getpid(), false) {
        struct stat status{};
        require(lstat(session_path(host).c_str(), &status) == 0, "Host session");
        if (!S_ISDIR(status.st_mode) || status.st_uid != geteuid() || (status.st_mode & 0777) != 0700)
            throw std::runtime_error("Unsafe host session directory");
        const auto mask = signal_set({SIGUSR2});
        const auto deadline = Clock::now() + std::chrono::seconds(timeout_seconds);
        bool acknowledged = false;
        while (Clock::now() < deadline) {
            require(kill(host, SIGUSR1) == 0, "kill(SIGUSR1)");
            timespec interval{0, 200'000'000};
            siginfo_t info{};
            const int result = sigtimedwait(&mask, &info, &interval);
            if (result == SIGUSR2 && info.si_pid == host && info.si_uid == geteuid() && info.si_value.sival_int == getpid()) {
                acknowledged = true;
                break;
            }
            if (result < 0 && errno != EAGAIN && errno != EINTR) require(false, "sigtimedwait");
        }
        if (!acknowledged) throw std::runtime_error("Handshake timeout (5 seconds)");
        channel.open();
        events.push("READY client " + std::to_string(getpid()) + " " + transport_name());
        reader = std::thread([this] {
            try {
                const auto termination = signal_set({SIGTERM, SIGINT});
                const timespec immediate{};
                while (!stop) {
                    if (sigtimedwait(&termination, nullptr, &immediate) >= 0) { stop = true; break; }
                    Message message;
                    if (channel.read(message, stop))
                        events.push(message.sequence == 0 ? "STATUS " + message.text : display(message));
                }
            } catch (const std::exception& error) {
                if (!stop) events.push(std::string("ERROR disconnected ") + error.what());
                stop = true;
            }
        });
    }
    ~Impl() { stop = true; if (reader.joinable()) reader.join(); }
};
Client::Client(int host, Events& events) : impl_(std::make_unique<Impl>(host, events)) {}
Client::~Client() = default;
void Client::send(int recipient, const std::string& text) {
    if (impl_->stop) throw std::runtime_error("Client disconnected");
    const Message message{0, now_ns(), static_cast<int>(getpid()), recipient, text};
    validate(message);
    try { impl_->channel.write(message); }
    catch (...) { impl_->stop = true; throw; }
}
bool Client::alive() const { return !impl_->stop; }
} // namespace chat
