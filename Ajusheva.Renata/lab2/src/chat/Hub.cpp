#include "chat/Hub.hpp"

#include "chat/TextClean.hpp"

#include <algorithm>

namespace chatlab {

Hub::Hub(std::chrono::milliseconds settle) : settle_(settle) {}

bool Hub::hasRoom() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seats_.size() < kMemberLimit;
}

bool Hub::nameBusy(const std::string& name) const {
    if (name == kHostName) {
        return true;
    }
    return std::any_of(seats_.begin(), seats_.end(),
                       [&name](const auto& entry) { return entry.second.name == name; });
}

std::optional<std::string> Hub::nameLocked(int id) const {
    if (id == kHostId) {
        return std::string(kHostName);
    }
    const auto found = seats_.find(id);
    if (found == seats_.end()) {
        return std::nullopt;
    }
    return found->second.name;
}

void Hub::noteOthers(int except, const std::string& note) {
    for (auto& [id, seat] : seats_) {
        if (id != except) {
            seat.notes.push_back(note);
        }
    }
}

std::string Hub::join(int id, pid_t pid, const std::string& wanted, Millis now) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string name = textclean::cleanName(wanted);
    if (name.empty()) {
        name = "Участник " + std::to_string(id);
    }
    if (nameBusy(name)) {
        name = textclean::cutUtf8(name, kNameLimit - 6) + " #" + std::to_string(id);
    }
    Seat seat;
    seat.pid = pid;
    seat.name = name;
    seat.lastSpoke = now;
    seats_[id] = std::move(seat);
    ++rosterStamp_;
    noteOthers(id, name + " (" + std::to_string(id) + ") вошёл в чат");
    return name;
}

void Hub::leave(int id, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = seats_.find(id);
    if (found == seats_.end()) {
        return;
    }
    const std::string name = found->second.name;
    seats_.erase(found);
    ++rosterStamp_;
    noteOthers(id, textclean::cutUtf8(name + " (" + std::to_string(id) + ") покинул чат: " + reason,
                                      kNoteLimit));
}

void Hub::notify(int id, const std::string& note) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = seats_.find(id);
    if (found != seats_.end()) {
        found->second.notes.push_back(textclean::cutUtf8(note, kNoteLimit));
    }
}

bool Hub::isMember(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return seats_.count(id) != 0;
}

std::optional<std::string> Hub::nameOf(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nameLocked(id);
}

std::vector<Member> Hub::roster() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Member> result{{kHostId, kHostName}};
    for (const auto& [id, seat] : seats_) {
        result.push_back({id, seat.name});
    }
    return result;
}

std::uint64_t Hub::rosterStamp() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rosterStamp_;
}

Accepted Hub::submit(int from, const Draft& draft, Millis now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Accepted result;
    const auto sender = nameLocked(from);
    if (!sender) {
        result.error = "отправитель не в чате";
        return result;
    }
    const std::string text = textclean::cleanText(draft.text);
    if (text.empty()) {
        result.error = "пустое сообщение";
        return result;
    }
    std::string receiverName;
    if (draft.to != kEveryone) {
        if (draft.to == from) {
            result.error = "нельзя написать лично самому себе";
            return result;
        }
        const auto receiver = nameLocked(draft.to);
        if (!receiver) {
            result.error = "нет участника с номером " + std::to_string(draft.to);
            return result;
        }
        receiverName = *receiver;
    }

    Post post;
    post.seq = nextSeq_++;
    post.sentAt = std::max(watermark_, draft.sentAt > 0 ? std::min(draft.sentAt, now) : now);
    post.from = from;
    post.to = draft.to;
    post.fromName = *sender;
    post.toName = receiverName;
    post.text = text;
    waiting_.push_back(post);

    const auto seat = seats_.find(from);
    if (seat != seats_.end()) {
        seat->second.lastSpoke = now;
    }
    result.ok = true;
    result.post = std::move(post);
    return result;
}

void Hub::deliver(int id, const Post& post) {
    const auto found = seats_.find(id);
    if (found != seats_.end()) {
        found->second.inbox.push_back(post);
    }
}

Released Hub::release(Millis now) {
    std::lock_guard<std::mutex> lock(mutex_);
    Released result;
    const Millis border = now - static_cast<Millis>(settle_.count());
    const auto due = std::stable_partition(waiting_.begin(), waiting_.end(),
                                           [border](const Post& post) { return post.sentAt <= border; });
    std::vector<Post> ready(waiting_.begin(), due);
    waiting_.erase(waiting_.begin(), due);
    std::sort(ready.begin(), ready.end(), earlier);

    for (const Post& post : ready) {
        watermark_ = std::max(watermark_, post.sentAt);
        if (!post.isPrivate()) {
            for (auto& [id, seat] : seats_) {
                seat.inbox.push_back(post);
            }
            result.forHost.push_back(post);
            continue;
        }
        deliver(post.from, post);
        deliver(post.to, post);
        if (post.from == kHostId || post.to == kHostId) {
            result.forHost.push_back(post);
        } else {
            result.relayed.push_back(post);
        }
    }
    return result;
}

Batch Hub::take(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    Batch batch;
    const auto found = seats_.find(id);
    if (found == seats_.end()) {
        return batch;
    }
    Seat& seat = found->second;
    while (!seat.inbox.empty() && batch.posts.size() < kPostsPerReply) {
        batch.posts.push_back(std::move(seat.inbox.front()));
        seat.inbox.pop_front();
    }
    while (!seat.notes.empty() && batch.notes.size() < kNotesPerReply) {
        batch.notes.push_back(std::move(seat.notes.front()));
        seat.notes.pop_front();
    }
    return batch;
}

std::vector<Silent> Hub::silentFor(Millis now, std::chrono::milliseconds limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Silent> result;
    for (const auto& [id, seat] : seats_) {
        if (now - seat.lastSpoke > static_cast<Millis>(limit.count())) {
            result.push_back({id, seat.pid, seat.name});
        }
    }
    return result;
}

}
