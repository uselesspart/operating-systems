#pragma once
#include "protocol.hpp"
#include <functional>

namespace application {
enum class EventKind { Status, Error, Message, Joined, Left, Stopped };
struct Event { EventKind kind; Packet packet; std::string text; };
using EventSink = std::function<void(const Event&)>;
void notify(const EventSink& sink, EventKind kind, const std::string& text,
            const Packet& packet = {});
} // namespace application
