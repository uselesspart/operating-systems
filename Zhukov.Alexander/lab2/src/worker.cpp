#include "worker.h"
#include "chat.h"

#include <iostream>
#include <map>

Worker::Worker(std::unique_ptr<Conn> connection, pid_t pid, bool create)
    : conn(std::move(connection)), host_pid(pid), host(create) {}

bool Worker::enqueue(int recipient, const QString& text) {
    if (!ready.load() || stopped.load()) {
        emit status_changed("Not connected");
        return false;
    }
    Message message;
    message.kind = recipient == -1 ? MessageKind::Public : MessageKind::Private;
    message.recipient = recipient;
    message.text = text.toUtf8().toStdString();
    message.number = 1;
    message.sent_at = std::chrono::duration_cast<std::chrono::nanoseconds>(
                          Clock::now().time_since_epoch()).count();
    try {
        Chat::validate(message);
    } catch (const std::exception& error) {
        emit status_changed(QString::fromUtf8(error.what()));
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex);
    if (outgoing.size() >= 64) {
        emit status_changed("Queue full");
        return false;
    }
    outgoing.push_back({std::move(message), Clock::now() + std::chrono::milliseconds(timeout_ms), false});
    return true;
}

void Worker::run() {
    int code = 0;
    try {
        Session session(std::move(conn), host_pid, host);
        emit status_changed(host ? "Waiting for client" : "Connecting");
        if (host) {
            while (!stopped.load() && !session.accept(stopped)) {}
        } else {
            session.connect(stopped);
        }
        if (!stopped.load()) {
            Chat chat(host ? 0 : 1);
            struct Pending {
                Message message;
                Deadline deadline;
            };
            std::map<std::uint64_t, Pending> pending;
            bool write_closed = false;
            Deadline drain_until{};
            ready.store(true);
            emit connected_changed(true);
            emit status_changed("Connected");
            while (!stopped.load()) {
                if (write_closed && Clock::now() >= drain_until) {
                    emit status_changed("Disconnected");
                    break;
                }
                Message message;
                bool queued = false;
                bool forwarded = false;
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    if (write_closed) {
                        outgoing.clear();
                    } else if (!outgoing.empty()) {
                        auto& next = outgoing.front();
                        if (Clock::now() >= next.deadline) {
                            throw std::runtime_error("Write timeout");
                        }
                        bool local = host && !next.forwarded &&
                                     next.message.kind == MessageKind::Private && next.message.recipient == 0;
                        if (local || session.writable()) {
                            message = std::move(next.message);
                            forwarded = next.forwarded;
                            outgoing.pop_front();
                            queued = true;
                        }
                    }
                }
                if (queued) {
                    try {
                        if (forwarded) {
                            session.forward(message, stopped);
                        } else if (host && message.kind == MessageKind::Private && message.recipient == 0) {
                            message = session.prepare(std::move(message));
                        } else {
                            message = session.send(std::move(message), stopped);
                        }
                        if (host && !forwarded) {
                            if (chat.accept(message)) {
                                emit history_changed(chat.history());
                            }
                        } else if (!host) {
                            pending.emplace(message.number, Pending{message,
                                Clock::now() + std::chrono::milliseconds(timeout_ms)});
                        }
                    } catch (const ConnectionClosed&) {
                        write_closed = true;
                        drain_until = Clock::now() + std::chrono::milliseconds(timeout_ms);
                        ready.store(false);
                        emit connected_changed(false);
                    }
                }
                auto result = session.receive(message, stopped, queued ? 0 : poll_ms);
                if (result == ReadResult::Closed) {
                    emit status_changed("Disconnected");
                    break;
                }
                if (result == ReadResult::Data) {
                    if (!host && message.sender == 1) {
                        auto found = pending.find(message.number);
                        if (found == pending.end()) {
                            continue;
                        }
                        const auto& sent = found->second.message;
                        if (message.text != sent.text || message.recipient != sent.recipient ||
                            message.sent_at != sent.sent_at || message.kind != sent.kind) {
                            throw std::runtime_error("Invalid confirmation");
                        }
                        pending.erase(found);
                    }
                    bool accepted = false;
                    try {
                        accepted = chat.accept(message);
                    } catch (const std::exception& error) {
                        emit status_changed(QString::fromUtf8(error.what()));
                    }
                    if (accepted) {
                        if (chat.visible(message)) {
                            emit history_changed(chat.history());
                        }
                        if (host && !write_closed) {
                            std::lock_guard<std::mutex> lock(mutex);
                            if (outgoing.size() >= 128) {
                                throw std::runtime_error("Queue full");
                            }
                            outgoing.push_back({message, Clock::now() +
                                                std::chrono::milliseconds(timeout_ms), true});
                        }
                    }
                }
                if (chat.idle()) {
                    session.kill_peer();
                    emit status_changed("Client inactive");
                    break;
                }
                for (const auto& entry : pending) {
                    if (Clock::now() >= entry.second.deadline) {
                        throw std::runtime_error("Delivery timeout");
                    }
                }
            }
        }
    } catch (const ConnectionClosed&) {
        if (!stopped.load()) { emit status_changed("Disconnected"); }
    } catch (const std::exception& error) {
        if (!stopped.load()) {
            std::cerr << error.what() << '\n';
            emit status_changed(QString::fromUtf8(error.what()));
            code = 1;
        }
    }
    ready.store(false);
    emit connected_changed(false);
    emit done(code);
}
