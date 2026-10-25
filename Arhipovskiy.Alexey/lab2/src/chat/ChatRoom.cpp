#include "chat/ChatRoom.h"

#include "chat/Text.h"

#include <algorithm>
#include <utility>

namespace chat {

ChatRoom::ChatRoom(std::chrono::milliseconds idleLimit, Clock clock)
    : idleLimit_(idleLimit), clock_(std::move(clock))
{
}

std::string ChatRoom::join(int id, const std::string& requestedName)
{
    std::lock_guard lock(mutex_);
    std::string name = text::sanitizeName(requestedName);
    if (name.empty()) {
        name = "Клиент " + std::to_string(id);
    }
    if (nameTaken(name)) {
        name += " (" + std::to_string(id) + ")";
    }

    members_[id] = Member{name, clock_(), {}, {}};
    ++rosterVersion_;
    notifyOthers(id, name + " присоединился к чату");
    return name;
}

void ChatRoom::leave(int id, const std::string& reason)
{
    std::lock_guard lock(mutex_);
    const auto member = members_.find(id);
    if (member == members_.end()) {
        return;
    }
    const std::string name = member->second.name;
    members_.erase(member);
    ++rosterVersion_;
    notifyOthers(id, name + " покинул чат (" + reason + ")");
}

bool ChatRoom::contains(int id) const
{
    std::lock_guard lock(mutex_);
    return members_.count(id) > 0;
}

std::vector<Participant> ChatRoom::participants() const
{
    std::lock_guard lock(mutex_);
    std::vector<Participant> result{{kHostId, kHostName}};
    for (const auto& [id, member] : members_) {
        result.push_back({id, member.name});
    }
    return result;
}

std::uint64_t ChatRoom::rosterVersion() const
{
    std::lock_guard lock(mutex_);
    return rosterVersion_;
}

PostResult ChatRoom::post(int from, const OutgoingMessage& message)
{
    std::lock_guard lock(mutex_);
    const std::optional<std::string> fromName = nameOf(from);
    if (!fromName) {
        return {std::nullopt, "Отправитель " + std::to_string(from) + " не в чате", false};
    }

    const std::string body = text::sanitizeText(message.text);
    if (body.empty()) {
        return reject(from, "Пустое сообщение не отправлено");
    }
    if (body.size() > kMaxTextBytes) {
        return reject(from, "Сообщение длиннее " + std::to_string(kMaxTextBytes) + " байт не отправлено");
    }
    if (message.to == from) {
        return reject(from, "Нельзя отправить личное сообщение самому себе");
    }
    std::string toName;
    if (message.to != kEveryone) {
        const std::optional<std::string> recipient = nameOf(message.to);
        if (!recipient) {
            return reject(from, "Участник " + std::to_string(message.to) + " не найден");
        }
        toName = *recipient;
    }

    const Millis now = clock_();
    ChatMessage accepted;
    accepted.seq = nextSeq_++;
    accepted.sentAtMs = (message.sentAtMs > 0 && message.sentAtMs <= now) ? message.sentAtMs : now;
    accepted.from = from;
    accepted.to = message.to;
    accepted.fromName = *fromName;
    accepted.toName = toName;
    accepted.text = body;

    for (auto& [id, member] : members_) {
        const bool addressed = !accepted.isPrivate() || id == accepted.to || id == accepted.from;
        if (addressed) {
            member.pending.push_back(accepted);
        }
    }
    if (const auto sender = members_.find(from); sender != members_.end()) {
        sender->second.lastActivityMs = now;
    }

    const bool visibleToHost = !accepted.isPrivate() || from == kHostId || accepted.to == kHostId;
    return {std::move(accepted), {}, visibleToHost};
}

Delivery ChatRoom::collect(int id)
{
    std::lock_guard lock(mutex_);
    const auto member = members_.find(id);
    if (member == members_.end()) {
        return {};
    }
    auto& pending = member->second.pending;
    std::sort(pending.begin(), pending.end(), sentBefore);

    const Millis readyBefore = clock_() - kOrderingDelay.count();
    std::size_t ready = 0;
    while (ready < pending.size() && ready < kMaxBatch && pending[ready].sentAtMs <= readyBefore) {
        ++ready;
    }

    Delivery delivery;
    delivery.messages.assign(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(ready));
    pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(ready));
    delivery.notices = std::move(member->second.notices);
    member->second.notices.clear();
    return delivery;
}

bool ChatRoom::isIdle(int id) const
{
    std::lock_guard lock(mutex_);
    const auto member = members_.find(id);
    return member != members_.end() && clock_() - member->second.lastActivityMs > idleLimit_.count();
}

bool ChatRoom::nameTaken(const std::string& name) const
{
    return name == kHostName || std::any_of(members_.begin(), members_.end(), [&name](const auto& entry) {
               return entry.second.name == name;
           });
}

std::optional<std::string> ChatRoom::nameOf(int id) const
{
    if (id == kHostId) {
        return kHostName;
    }
    const auto member = members_.find(id);
    if (member == members_.end()) {
        return std::nullopt;
    }
    return member->second.name;
}

void ChatRoom::notifyOthers(int except, const std::string& notice)
{
    for (auto& [id, member] : members_) {
        if (id != except) {
            member.notices.push_back(notice);
        }
    }
}

PostResult ChatRoom::reject(int from, std::string error)
{
    if (const auto sender = members_.find(from); sender != members_.end()) {
        sender->second.notices.push_back(error);
    }
    return {std::nullopt, std::move(error), false};
}

}
