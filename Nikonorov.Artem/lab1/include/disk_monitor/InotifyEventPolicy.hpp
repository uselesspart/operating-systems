#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/inotify.h>

namespace diskmon {

constexpr bool inotifyRecordFits(std::size_t available, std::size_t offset,
                                 std::size_t nameLength) noexcept {
    return offset <= available && sizeof(inotify_event) <= available - offset &&
           nameLength <= available - offset - sizeof(inotify_event);
}

constexpr bool inotifyEventRequiresReconciliation(std::uint32_t mask) noexcept {
    constexpr std::uint32_t recoveryMask = IN_Q_OVERFLOW | IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT;
    return (mask & recoveryMask) != 0U;
}

constexpr bool inotifyEventDiscoversDirectory(std::uint32_t mask) noexcept {
    return (mask & IN_ISDIR) != 0U && (mask & (IN_CREATE | IN_MOVED_TO)) != 0U;
}

} 
