#include "disk_monitor/WatchManager.hpp"

#include "disk_monitor/Config.hpp"
#include "disk_monitor/InotifyEventPolicy.hpp"
#include "disk_monitor/Logging.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <utility>
#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

namespace diskmon {
namespace {

constexpr std::uint32_t kDirectoryOpenNoise = IN_OPEN | IN_ACCESS | IN_CLOSE_NOWRITE;

std::string describeEventMask(std::uint32_t mask) {
    std::vector<std::string> names;
    const std::array<std::pair<std::uint32_t, const char*>, 15> eventBits{{
        {IN_ACCESS, "ACCESS"},
        {IN_MODIFY, "MODIFY"},
        {IN_ATTRIB, "ATTRIB"},
        {IN_CLOSE_WRITE, "CLOSE_WRITE"},
        {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
        {IN_OPEN, "OPEN"},
        {IN_MOVED_FROM, "MOVED_FROM"},
        {IN_MOVED_TO, "MOVED_TO"},
        {IN_CREATE, "CREATE"},
        {IN_DELETE, "DELETE"},
        {IN_DELETE_SELF, "DELETE_SELF"},
        {IN_MOVE_SELF, "MOVE_SELF"},
        {IN_UNMOUNT, "UNMOUNT"},
        {IN_Q_OVERFLOW, "Q_OVERFLOW"},
        {IN_IGNORED, "IGNORED"},
    }};
    for (const auto& bit : eventBits) {
        if ((mask & bit.first) != 0U) {
            names.emplace_back(bit.second);
        }
    }
    if (names.empty()) {
        names.emplace_back("UNKNOWN");
    }

    std::ostringstream output;
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (index != 0U) {
            output << '|';
        }
        output << names[index];
    }
    return output.str();
}

std::string objectType(std::uint32_t mask, bool nameIsEmpty) {
    if (nameIsEmpty || (mask & IN_ISDIR) != 0U) {
        return "directory";
    }
    return "file";
}


}  // namespace

void WatchManager::processAvailableEvents() {
    if (descriptor_ < 0) {
        return;
    }
    alignas(inotify_event) std::array<char, 64 * 1024> buffer{};
    bool reconciliationRequired = false;
    bool overflowSeen = false;

    for (;;) {
        const ssize_t bytesRead = ::read(descriptor_, buffer.data(), buffer.size());
        if (bytesRead < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            logDaemon(LOG_ERR, "reading inotify queue failed: " + std::string(std::strerror(errno)));
            reconciliationRequired = true;
            break;
        }
        if (bytesRead == 0) {
            break;
        }

        std::size_t offset = 0;
        const std::size_t available = static_cast<std::size_t>(bytesRead);
        while (offset + sizeof(inotify_event) <= available) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer.data() + offset);
            const std::size_t nameLength = static_cast<std::size_t>(event->len);
            const std::size_t recordSize = sizeof(inotify_event) + nameLength;
            if (!inotifyRecordFits(available, offset, nameLength)) {
                logDaemon(LOG_ERR, "malformed/truncated record in inotify buffer; scheduling reconciliation");
                reconciliationRequired = true;
                break;
            }

            if (processEvent(*event)) {
                reconciliationRequired = true;
            }
            if ((event->mask & IN_Q_OVERFLOW) != 0U) {
                overflowSeen = true;
            }
            if (inotifyEventRequiresReconciliation(event->mask)) {
                reconciliationRequired = true;
            }
            offset += recordSize;
        }
    }

    if (!pendingDirectoryMoves_.empty()) {
        pendingDirectoryMoves_.clear();
        reconciliationRequired = true;
    }
    if (overflowSeen) {
        logDaemon(LOG_WARNING, "inotify queue overflow detected; events may have been lost, rebuilding recursive watches");
    }
    if (reconciliationRequired) {
        (void)reconcile();
    }
}

bool WatchManager::processEvent(const inotify_event& event) {
    if ((event.mask & IN_IGNORED) != 0U) {
        if (pendingIgnored_.erase(event.wd) == 0U) {
            const auto mapping = watchToPath_.find(event.wd);
            if (mapping != watchToPath_.end()) {
                const auto reverse = pathToWatch_.find(mapping->second);
                if (reverse != pathToWatch_.end() && reverse->second == event.wd) {
                    pathToWatch_.erase(reverse);
                }
                watchToPath_.erase(mapping);
            }
        }
        return false;
    }
    if ((event.mask & IN_Q_OVERFLOW) != 0U) {
        return true;
    }

    const auto mapping = watchToPath_.find(event.wd);
    if (mapping == watchToPath_.end()) {
        return false;
    }
    const std::filesystem::path directory(mapping->second);
    std::string name;
    if (event.len > 0U) {
        const std::size_t maxNameLength = static_cast<std::size_t>(event.len);
        const char* nameStart = event.name;
        const void* terminator = std::memchr(nameStart, '\0', maxNameLength);
        const std::size_t nameLength = terminator == nullptr
            ? maxNameLength
            : static_cast<std::size_t>(static_cast<const char*>(terminator) - nameStart);
        name.assign(nameStart, nameLength);
    }

    std::filesystem::path fullPath = directory;
    if (!name.empty()) {
        fullPath /= name;
    }
    fullPath = fullPath.lexically_normal();

    const std::string directoryKey = absoluteNormalizedPath(directory).string();
    const bool isRootAnchor = std::any_of(rootAnchorByRoot_.begin(), rootAnchorByRoot_.end(),
        [&directoryKey](const auto& entry) { return entry.second == directoryKey; });
    const bool relevantAnchorEvent = isRootAnchor && anchorEventMatchesRoot(directory, name, event.mask);

    if (isAnchorOnlyPath(directory)) {
        return relevantAnchorEvent;
    }

    const bool pureDirectoryOpenNoise = (event.mask & kDirectoryOpenNoise) != 0U &&
        (event.mask & ~(kDirectoryOpenNoise | IN_ISDIR)) == 0U;
    if (pureDirectoryOpenNoise &&
        (((event.mask & IN_ISDIR) != 0U) || name.empty())) {
        return false;
    }
    if (isLogSinkPath(fullPath)) {
        return false;
    }

    std::ostringstream message;
    message << "time=" << currentTimestamp()
            << " source=kernel"
            << " event=" << describeEventMask(event.mask)
            << " object_type=" << objectType(event.mask, name.empty())
            << " directory=" << sanitizeLogField(directory.string())
            << " name=" << sanitizeLogField(name)
            << " path=" << sanitizeLogField(fullPath.string());
    logFilesystemEvent(message.str());

    if ((event.mask & IN_ISDIR) != 0U && (event.mask & IN_MOVED_FROM) != 0U && event.cookie != 0U) {
        pendingDirectoryMoves_[event.cookie] = fullPath;
    }
    if (inotifyEventDiscoversDirectory(event.mask)) {
        if ((event.mask & IN_MOVED_TO) != 0U) {
            const auto movedFrom = pendingDirectoryMoves_.find(event.cookie);
            if (event.cookie != 0U && movedFrom != pendingDirectoryMoves_.end()) {
                const std::filesystem::path oldPath = movedFrom->second;
                pendingDirectoryMoves_.erase(movedFrom);
                rebaseWatches(oldPath, fullPath);
            }
        }
        (void)scanNewDirectory(fullPath);
    }
    return relevantAnchorEvent;
}
}  // namespace diskmon
