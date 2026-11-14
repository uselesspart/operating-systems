#include "host/HostServer.h"

#include "conn/conn.h"
#include "ipc/Channel.h"
#include "ipc/Timeouts.h"
#include "util/Log.h"

#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

#include <signal.h>
#include <unistd.h>

namespace chat {

namespace {

using SteadyClock = std::chrono::steady_clock;

// Semaphore waits are split into short slices so that a stopping host notices it quickly
constexpr auto kWaitSlice = std::chrono::milliseconds(200);
constexpr auto kShutdownGrace = std::chrono::seconds(1);

std::string clientLabel(int id, const std::string& name)
{
    return "Клиент " + std::to_string(id) + " «" + name + "»";
}

std::string describeRecipient(const ChatMessage& message)
{
    if (!message.isPrivate()) {
        return "everyone";
    }
    return message.to == kHostId ? "host" : "client " + std::to_string(message.to);
}

std::string describeFailure(ChannelStatus status)
{
    return status == ChannelStatus::Corrupted ? "получены повреждённые данные" : "соединение разорвано";
}

}

struct HostServer::Session {
    Session(int id, pid_t pid, std::unique_ptr<Channel> channel)
        : id(id), pid(pid), channel(std::move(channel))
    {
    }

    const int id;
    const pid_t pid;
    std::unique_ptr<Channel> channel;
    std::string name;
    std::uint64_t sentRosterVersion = 0;
    std::thread thread;
    std::atomic<bool> finished{false};
};

HostServer::HostServer(HostListener& listener, HostOptions options)
    : listener_(listener), options_(options), room_(options.idleLimit),
      signals_([this](pid_t pid) { handleHandshake(pid); }, [this](int) { listener_.onShutdownRequested(); })
{
}

HostServer::~HostServer()
{
    stop();
}

void HostServer::start()
{
    RuntimeDir::removeStale();
    runtimeDir_ = std::make_unique<RuntimeDir>(::getpid());
    signals_.start();
    started_ = true;

    const std::string pid = std::to_string(::getpid());
    log::info("Host started: pid " + pid + ", connection type " + Conn::typeCode());
    listener_.onStatus("Хост запущен: pid " + pid + ", тип связи " + Conn::typeCode());
    notifyParticipants();
}

void HostServer::stop()
{
    if (!started_) {
        return;
    }
    started_ = false;
    stopping_ = true;
    signals_.stop();

    std::map<int, std::unique_ptr<Session>> sessions;
    {
        std::lock_guard lock(sessionsMutex_);
        sessions.swap(sessions_);
    }
    for (auto& [id, session] : sessions) {
        if (session->thread.joinable()) {
            session->thread.join();
        }
    }
    sessions.clear();
    runtimeDir_.reset();
    log::info("Host stopped");
}

void HostServer::sendMessage(int to, const std::string& text)
{
    const PostResult result = room_.post(kHostId, {to, wallClockMs(), text});
    if (!result.message) {
        listener_.onStatus(result.error);
        return;
    }
    log::info("Message #" + std::to_string(result.message->seq) + " from host to "
              + describeRecipient(*result.message));
    listener_.onMessage(*result.message);
}

void HostServer::handleHandshake(pid_t pid)
{
    if (stopping_) {
        return;
    }
    std::lock_guard lock(sessionsMutex_);
    reapFinishedLocked();

    // Standard signals are not queued, so clients repeat the request; answer repeats with the same id
    for (const auto& [id, session] : sessions_) {
        if (session->pid == pid) {
            acceptHandshake(pid, id);
            return;
        }
    }
    if (sessions_.size() >= kMaxParticipants) {
        log::warning("Handshake from pid " + std::to_string(pid) + " rejected: chat is full");
        listener_.onStatus("Запрос от процесса " + std::to_string(pid) + " отклонён: чат заполнен");
        return;
    }

    const int id = nextClientId_++;
    std::unique_ptr<Channel> channel;
    try {
        channel = std::make_unique<Channel>(ConnId{::getpid(), id}, Channel::Side::Host);
    } catch (const std::exception& e) {
        log::error("Cannot create channel for pid " + std::to_string(pid) + ": " + e.what());
        listener_.onStatus("Не удалось создать канал для процесса " + std::to_string(pid) + ": " + e.what());
        return;
    }
    if (!acceptHandshake(pid, id)) {
        log::warning("Handshake reply to pid " + std::to_string(pid) + " failed, process is gone");
        return;
    }

    log::info("Handshake from pid " + std::to_string(pid) + ": client id " + std::to_string(id));
    listener_.onStatus("Запрос на подключение от процесса " + std::to_string(pid) + ", выдан номер "
                       + std::to_string(id));
    auto session = std::make_unique<Session>(id, pid, std::move(channel));
    Session& started = *session;
    sessions_[id] = std::move(session);
    started.thread = std::thread(&HostServer::serve, this, std::ref(started));
}

void HostServer::serve(Session& session)
{
    std::string failure;
    std::optional<protocol::Request> request = awaitRequest(session, failure);
    if (!request || request->kind != protocol::RequestKind::Join) {
        finish(session, false, request ? "первый запрос не является приветствием" : failure);
        return;
    }

    session.name = room_.join(session.id, request->name);
    log::info("Client " + std::to_string(session.id) + " (" + session.name + ", pid "
              + std::to_string(session.pid) + ") joined");
    listener_.onStatus(clientLabel(session.id, session.name) + " присоединился (pid "
                       + std::to_string(session.pid) + ")");
    notifyParticipants();

    protocol::Response welcome;
    welcome.kind = protocol::ResponseKind::Welcome;
    if (!reply(session, std::move(welcome))) {
        finish(session, true, "соединение разорвано");
        return;
    }

    while (true) {
        request = awaitRequest(session, failure);
        if (!request) {
            finish(session, true, failure);
            return;
        }
        if (request->kind == protocol::RequestKind::Leave) {
            protocol::Response bye;
            bye.kind = protocol::ResponseKind::Bye;
            bye.reason = "вы вышли из чата";
            reply(session, std::move(bye));
            finish(session, true, "вышел");
            return;
        }

        relay(session, request->outgoing);

        if (room_.isIdle(session.id)) {
            log::warning("Client " + std::to_string(session.id) + " idle for more than "
                         + std::to_string(options_.idleLimit.count() / 1000) + " s: SIGKILL to pid "
                         + std::to_string(session.pid));
            ::kill(session.pid, SIGKILL);
            finish(session, true,
                   "не писал дольше " + std::to_string(options_.idleLimit.count() / 1000)
                       + " с, отправлен SIGKILL");
            return;
        }
        if (stopping_) {
            protocol::Response bye;
            bye.kind = protocol::ResponseKind::Bye;
            bye.reason = "хост завершает работу";
            reply(session, std::move(bye));
            finish(session, true, "хост завершает работу");
            return;
        }
        if (!reply(session, protocol::Response{})) {
            finish(session, true, "соединение разорвано");
            return;
        }
    }
}

std::optional<protocol::Request> HostServer::awaitRequest(Session& session, std::string& failure)
{
    const auto deadline = SteadyClock::now() + kIpcTimeout;
    std::optional<SteadyClock::time_point> shutdownDeadline;
    Bytes frame;
    while (true) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - SteadyClock::now());
        const auto slice = std::max(std::chrono::milliseconds(0), std::min(left, kWaitSlice));
        const ChannelStatus status = session.channel->receive(frame, slice);
        if (status == ChannelStatus::Ok) {
            try {
                return protocol::decodeRequest(frame);
            } catch (const protocol::ProtocolError& e) {
                log::warning("Client " + std::to_string(session.id) + " sent a bad request: " + e.what());
                failure = "получены повреждённые данные";
                return std::nullopt;
            }
        }
        if (status != ChannelStatus::Timeout) {
            failure = describeFailure(status);
            return std::nullopt;
        }

        const auto now = SteadyClock::now();
        if (stopping_) {
            // Give the client one more poll cycle to receive a proper goodbye
            if (!shutdownDeadline) {
                shutdownDeadline = now + kShutdownGrace;
            }
            if (now >= *shutdownDeadline) {
                failure = "хост завершает работу";
                return std::nullopt;
            }
        }
        if (now >= deadline) {
            failure = "не отвечает дольше " + std::to_string(kIpcTimeout.count() / 1000) + " с";
            return std::nullopt;
        }
    }
}

bool HostServer::reply(Session& session, protocol::Response response)
{
    response.clientId = session.id;
    const std::uint64_t version = room_.rosterVersion();
    if (version != session.sentRosterVersion) {
        response.participants = room_.participants();
        session.sentRosterVersion = version;
    }
    Delivery delivery = room_.collect(session.id);
    response.messages = std::move(delivery.messages);
    response.notices = std::move(delivery.notices);
    return session.channel->send(protocol::encode(response)) == ChannelStatus::Ok;
}

void HostServer::relay(Session& session, const std::vector<OutgoingMessage>& outgoing)
{
    for (const OutgoingMessage& message : outgoing) {
        const PostResult result = room_.post(session.id, message);
        if (!result.message) {
            continue;
        }
        const ChatMessage& accepted = *result.message;
        log::info("Message #" + std::to_string(accepted.seq) + " from client " + std::to_string(session.id)
                  + " to " + describeRecipient(accepted));
        if (result.visibleToHost) {
            listener_.onMessage(accepted);
        } else {
            listener_.onStatus("Передано личное сообщение: " + accepted.fromName + " → " + accepted.toName);
        }
    }
}

void HostServer::finish(Session& session, bool joined, const std::string& reason)
{
    const std::string label = clientLabel(session.id, joined ? session.name : "без имени");
    log::info("Client " + std::to_string(session.id) + " disconnected: " + reason);
    if (joined) {
        room_.leave(session.id, reason);
        notifyParticipants();
    }
    listener_.onStatus(label + " отключён: " + reason);
    session.channel.reset();
    session.finished = true;
}

void HostServer::reapFinishedLocked()
{
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (it->second->finished) {
            it->second->thread.join();
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

void HostServer::notifyParticipants()
{
    listener_.onParticipantsChanged(room_.participants());
}

}
