#include "client/ChatClient.h"

#include "chat/Limits.h"
#include "ipc/Signals.h"
#include "ipc/Timeouts.h"
#include "util/Log.h"

#include <algorithm>
#include <utility>

namespace chat {

namespace {

std::string describeFailure(ChannelStatus status)
{
    switch (status) {
    case ChannelStatus::Timeout:
        return "хост не отвечает дольше " + std::to_string(kIpcTimeout.count() / 1000) + " с";
    case ChannelStatus::Corrupted:
        return "получены повреждённые данные";
    default:
        return "соединение с хостом разорвано";
    }
}

}

ChatClient::ChatClient(pid_t hostPid, std::string requestedName, ClientListener& listener)
    : hostPid_(hostPid), requestedName_(std::move(requestedName)), listener_(listener)
{
}

ChatClient::~ChatClient()
{
    leave();
}

void ChatClient::connect()
{
    const std::string host = std::to_string(hostPid_);
    const HandshakeResult handshake = requestHandshake(hostPid_);
    if (handshake.status == HandshakeStatus::NoHost) {
        throw ClientError("Процесс " + host + " не найден или недоступен");
    }
    if (handshake.status == HandshakeStatus::Timeout) {
        throw ClientError("Хост " + host + " не ответил на SIGUSR1 за "
                          + std::to_string(kIpcTimeout.count() / 1000) + " с");
    }
    id_ = handshake.clientId;
    log::info("Handshake with host " + host + ": client id " + std::to_string(id_));

    try {
        channel_ = std::make_unique<Channel>(ConnId{hostPid_, id_}, Channel::Side::Client);
    } catch (const std::exception& e) {
        throw ClientError(std::string("Не удалось открыть канал связи: ") + e.what());
    }

    protocol::Request join;
    join.kind = protocol::RequestKind::Join;
    join.name = requestedName_;
    std::string failure;
    const std::optional<protocol::Response> welcome = exchange(join, failure);
    if (!welcome) {
        throw ClientError("Хост не принял подключение: " + failure);
    }
    if (welcome->kind != protocol::ResponseKind::Welcome) {
        throw ClientError("Хост не принял подключение: " + welcome->reason);
    }
    running_ = true;
    apply(*welcome);
}

void ChatClient::start()
{
    thread_ = std::thread(&ChatClient::run, this);
}

void ChatClient::send(int to, const std::string& text)
{
    {
        std::lock_guard lock(mutex_);
        outbox_.push_back({to, wallClockMs(), text});
    }
    wakeUp_.notify_one();
}

void ChatClient::leave()
{
    {
        std::lock_guard lock(mutex_);
        leaveRequested_ = true;
    }
    wakeUp_.notify_one();
    wait();
}

void ChatClient::wait()
{
    if (thread_.joinable()) {
        thread_.join();
    }
}

std::string ChatClient::name() const
{
    std::lock_guard lock(mutex_);
    const auto self = std::find_if(participants_.begin(), participants_.end(),
                                   [this](const Participant& participant) { return participant.id == id_; });
    return self != participants_.end() ? self->name : std::string();
}

std::vector<Participant> ChatClient::participants() const
{
    std::lock_guard lock(mutex_);
    return participants_;
}

void ChatClient::run()
{
    while (true) {
        protocol::Request request;
        bool leaving = false;
        {
            std::unique_lock lock(mutex_);
            wakeUp_.wait_for(lock, kPollInterval, [this] { return !outbox_.empty() || leaveRequested_; });
            while (!outbox_.empty() && request.outgoing.size() < kMaxBatch) {
                request.outgoing.push_back(std::move(outbox_.front()));
                outbox_.pop_front();
            }
            leaving = leaveRequested_ && request.outgoing.empty();
        }
        request.kind = leaving ? protocol::RequestKind::Leave : protocol::RequestKind::Poll;

        std::string failure;
        const std::optional<protocol::Response> response = exchange(request, failure);
        if (!response) {
            disconnect(failure, true);
            return;
        }
        apply(*response);
        if (response->kind == protocol::ResponseKind::Bye) {
            disconnect(response->reason, false);
            return;
        }
    }
}

std::optional<protocol::Response> ChatClient::exchange(const protocol::Request& request, std::string& failure)
{
    ChannelStatus status = channel_->send(protocol::encode(request));
    Bytes frame;
    if (status == ChannelStatus::Ok) {
        status = channel_->receive(frame, kIpcTimeout);
    }
    if (status != ChannelStatus::Ok) {
        failure = describeFailure(status);
        return std::nullopt;
    }
    try {
        return protocol::decodeResponse(frame);
    } catch (const protocol::ProtocolError& e) {
        log::warning(std::string("Bad response from host: ") + e.what());
        failure = describeFailure(ChannelStatus::Corrupted);
        return std::nullopt;
    }
}

void ChatClient::apply(const protocol::Response& response)
{
    if (response.participants) {
        std::lock_guard lock(mutex_);
        participants_ = *response.participants;
    }
    for (const ChatMessage& message : response.messages) {
        listener_.onMessage(message);
    }
    for (const std::string& notice : response.notices) {
        listener_.onNotice(notice);
    }
}

void ChatClient::disconnect(const std::string& reason, bool lost)
{
    log::info("Disconnected from host: " + reason);
    lost_ = lost;
    running_ = false;
    listener_.onDisconnected(reason, lost);
}

}
