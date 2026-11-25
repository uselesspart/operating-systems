#include "client.hpp"
#include "ipc.hpp"
#include <atomic>
#include <condition_variable>
#include <csignal>
#include <deque>
#include <thread>
#include <unistd.h>

namespace application {
struct Client::Impl {
    ClientConfig config;
    EventSink sink;
    const int pid = getpid();
    std::shared_ptr<IConnection> connection;
    std::atomic<bool> running{false};
    std::atomic<bool> leave{false};
    std::atomic<bool> stopped{false};
    bool started = false;
    std::jthread receiver;
    std::jthread sender;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Packet> queue;
    Impl(ClientConfig value, EventSink callback) : config(std::move(value)), sink(std::move(callback)) {}
    void stop(bool graceful = false) noexcept {
        if (graceful && running) { leave = true; }
        running = false;
        ready.notify_all();
    }
    void stopped_event() {
        if (!stopped.exchange(true)) { notify(sink, EventKind::Stopped, "Client stopped"); }
    }
    void send_loop() {
        try {
            while (running) {
                Packet packet;
                {
                    std::unique_lock lock(mutex);
                    ready.wait(lock, [this] { return !running || !queue.empty(); });
                    if (!running) { break; }
                    packet = std::move(queue.front());
                    queue.pop_front();
                }
                connection->send(encode(packet));
            }
            if (leave) {
                Packet goodbye;
                goodbye.kind = Kind::Leave;
                goodbye.sender = pid;
                connection->send(encode(goodbye));
            }
        } catch (const std::exception& error) {
            if (running) { notify(sink, EventKind::Error, error.what()); }
            stop();
        }
        connection->close();
    }
    void receive_loop() {
        try {
            std::uint64_t last_sequence = 0;
            while (running) {
                if (!process_alive(config.host_pid)) { throw Error("Host process has terminated"); }
                if (wait_signal(false, std::chrono::milliseconds(0)).first) { stop(true); break; }
                const auto bytes = connection->receive(std::chrono::milliseconds(100));
                if (!bytes) { continue; }
                const auto packet = decode(*bytes);
                switch (packet.kind) {
                case Kind::Chat:
                    if (packet.sequence <= last_sequence) { throw Error("Message ordering violation"); }
                    if (packet.recipient && packet.recipient != pid && packet.sender != pid) {
                        throw Error("Private message recipient mismatch");
                    }
                    last_sequence = packet.sequence;
                    notify(sink, EventKind::Message, {}, packet);
                    break;
                case Kind::Joined: notify(sink, EventKind::Joined, "Connected", packet); break;
                case Kind::Left: notify(sink, EventKind::Left, packet.text, packet); break;
                case Kind::Failure: notify(sink, EventKind::Error, packet.text); break;
                case Kind::Shutdown:
                    notify(sink, EventKind::Status, packet.text);
                    stop();
                    break;
                default: throw Error("Unexpected host packet");
                }
            }
        } catch (const std::exception& error) {
            if (running) { notify(sink, EventKind::Error, error.what()); }
            stop();
        }
        stopped_event();
    }
};
Client::Client(ClientConfig config, EventSink sink)
    : impl_(std::make_unique<Impl>(std::move(config), std::move(sink))) {}
Client::~Client() { request_stop(); join(); }
void Client::start() {
    auto& app = *impl_;
    if (app.started) { throw Error("Client cannot be started twice"); }
    app.started = true;
    validate_name(app.config.name);
    if (app.config.host_pid <= 1 || app.config.host_pid == app.pid || !process_alive(app.config.host_pid)) {
        throw Error("Host PID is unavailable or invalid");
    }
    const auto root = root_path(app.config.host_pid);
    check_directory(root);
    NamedSemaphore registration("/pn" + std::string(TYPE_CODE) + "_" +
        std::to_string(app.config.host_pid) + "_join", false, 1);
    registration.wait();
    try {
        if (kill(app.config.host_pid, SIGUSR1) < 0) { system_error("SIGUSR1"); }
        wait_for_ack(app.config.host_pid);
        app.connection = connect_to_host(root, app.pid);
        Packet hello;
        hello.kind = Kind::Hello;
        hello.sender = app.pid;
        hello.name = app.config.name;
        app.connection->send(encode(hello));
        const auto bytes = app.connection->receive(ipc_timeout);
        if (!bytes) { throw Error("WELCOME timed out after 5 seconds"); }
        const auto welcome = decode(*bytes);
        if (welcome.kind == Kind::Failure) { throw Error(welcome.text); }
        if (welcome.kind != Kind::Welcome || welcome.sender != app.config.host_pid ||
            welcome.recipient != app.pid) { throw Error("Invalid WELCOME"); }
    } catch (...) { registration.post(); throw; }
    registration.post();
    app.running = true;
    app.sender = std::jthread([&app] { app.send_loop(); });
    app.receiver = std::jthread([&app] { app.receive_loop(); });
    notify(app.sink, EventKind::Status, "READY client=" + std::to_string(app.pid) +
        " host=" + std::to_string(app.config.host_pid) + " transport=" + TYPE_CODE);
}
void Client::post_message(const std::string& text, int recipient) {
    if (!running()) { throw Error("Client is not connected"); }
    validate_text(text);
    if (recipient < 0) { throw Error("Invalid recipient PID"); }
    Packet packet;
    packet.sender = impl_->pid;
    packet.recipient = recipient;
    packet.text = text;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->queue.size() >= 128) { throw Error("Outgoing message queue is full"); }
        impl_->queue.push_back(std::move(packet));
    }
    impl_->ready.notify_one();
}
void Client::request_stop() noexcept { impl_->stop(true); }
void Client::join() {
    if (impl_->sender.joinable()) { impl_->sender.join(); }
    if (impl_->receiver.joinable()) { impl_->receiver.join(); }
    impl_->connection.reset();
}
bool Client::running() const noexcept { return impl_->running; }
} // namespace application
