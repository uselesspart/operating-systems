#include "client/GuestSession.hpp"

#include "link/Signals.hpp"
#include "link/Timeouts.hpp"
#include "util/Journal.hpp"

#include <utility>

namespace chatlab {

GuestSession::GuestSession(pid_t host, std::string wantedName)
    : host_(host), wantedName_(std::move(wantedName)) {}

packets::Reply GuestSession::connect() {
    const std::string hostText = std::to_string(host_);
    const sigctl::KnockResult knock = sigctl::knock(host_);
    switch (knock.outcome) {
    case sigctl::KnockOutcome::NoHost:
        throw GuestError("процесс " + hostText + " не найден или недоступен");
    case sigctl::KnockOutcome::NoAnswer:
        throw GuestError("хост " + hostText + " не ответил на SIGUSR1 за 5 с");
    case sigctl::KnockOutcome::Interrupted:
        throw GuestError("подключение прервано сигналом");
    case sigctl::KnockOutcome::Admitted:
        break;
    }
    id_ = knock.slot;
    journal::info("знакомство с хостом " + hostText + " завершено, номер " + std::to_string(id_));

    try {
        link_ = std::make_unique<Link>(ConnKey{host_, id_}, Link::Side::Guest);
    } catch (const std::exception& error) {
        throw GuestError(std::string("не удалось открыть канал: ") + error.what());
    }

    packets::Request hello;
    hello.ask = packets::Ask::Hello;
    hello.name = wantedName_;
    Exchange result = roundTrip(hello);
    if (!result.ok) {
        throw GuestError("хост не принял приветствие: " + result.failure);
    }
    if (result.reply.answer != packets::Answer::Welcome) {
        throw GuestError("хост отказал: " + result.reply.reason);
    }
    return result.reply;
}

Exchange GuestSession::poll(std::vector<Draft> drafts) {
    packets::Request request;
    request.ask = packets::Ask::Poll;
    request.drafts = std::move(drafts);
    return roundTrip(request);
}

Exchange GuestSession::leave(std::vector<Draft> drafts) {
    packets::Request request;
    request.ask = packets::Ask::Bye;
    request.drafts = std::move(drafts);
    return roundTrip(request);
}

Exchange GuestSession::roundTrip(const packets::Request& request) {
    Exchange result;
    const LinkStatus sent = link_->send(packets::pack(request));
    if (sent != LinkStatus::Ok) {
        result.failure = statusText(sent);
        return result;
    }
    wire::Bytes frame;
    const LinkStatus received = link_->receive(frame, kWaitLimit);
    if (received != LinkStatus::Ok) {
        result.failure = statusText(received);
        return result;
    }
    try {
        result.reply = packets::unpackReply(frame);
    } catch (const wire::Error& error) {
        result.failure = std::string("повреждённый ответ: ") + error.what();
        return result;
    }
    result.ok = true;
    return result;
}

}
