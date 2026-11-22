#include "EventFormatter.h"

#include <sys/inotify.h>

#include <array>
#include <sstream>

namespace disk_monitor {
namespace {

struct EventName {
    std::uint32_t mask;
    const char* name;
};

constexpr std::array<EventName, 14> event_names = {{
    {IN_ACCESS, "ACCESS"},
    {IN_ATTRIB, "ATTRIB"},
    {IN_CLOSE_WRITE, "CLOSE_WRITE"},
    {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
    {IN_CREATE, "CREATE"},
    {IN_DELETE, "DELETE"},
    {IN_DELETE_SELF, "DELETE_SELF"},
    {IN_MODIFY, "MODIFY"},
    {IN_MOVED_FROM, "MOVED_FROM"},
    {IN_MOVED_TO, "MOVED_TO"},
    {IN_MOVE_SELF, "MOVE_SELF"},
    {IN_OPEN, "OPEN"},
    {IN_IGNORED, "IGNORED"},
    {IN_Q_OVERFLOW, "QUEUE_OVERFLOW"},
}};

}  // namespace

std::string eventMaskToText(const std::uint32_t mask) {
    std::ostringstream result;
    bool has_name = false;
    for (const auto& event : event_names) {
        if ((mask & event.mask) == 0) {
            continue;
        }
        if (has_name) {
            result << '|';
        }
        result << event.name;
        has_name = true;
    }
    return has_name ? result.str() : "UNKNOWN";
}

std::string eventKindToText(const std::uint32_t mask) {
    return (mask & IN_ISDIR) == 0 ? "file" : "directory";
}

}  // namespace disk_monitor

