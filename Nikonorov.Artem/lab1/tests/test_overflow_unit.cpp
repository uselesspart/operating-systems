#include "disk_monitor/InotifyEventPolicy.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sys/inotify.h>

int main() {
    const std::size_t headerSize = sizeof(inotify_event);
    if (!diskmon::inotifyRecordFits(headerSize, 0U, 0U)) {
        std::cerr << "FAIL: valid zero-name inotify record rejected\n";
        return 1;
    }
    if (diskmon::inotifyRecordFits(headerSize, 0U, 1U)) {
        std::cerr << "FAIL: truncated inotify record accepted\n";
        return 1;
    }
    if (!diskmon::inotifyEventRequiresReconciliation(IN_Q_OVERFLOW)) {
        std::cerr << "FAIL: IN_Q_OVERFLOW does not request reconciliation\n";
        return 1;
    }
    if (!diskmon::inotifyEventRequiresReconciliation(IN_IGNORED | IN_ISDIR)) {
        std::cerr << "FAIL: IN_IGNORED does not request reconciliation\n";
        return 1;
    }
    if (!diskmon::inotifyEventDiscoversDirectory(IN_CREATE | IN_ISDIR)) {
        std::cerr << "FAIL: directory CREATE is not discovered\n";
        return 1;
    }
    if (diskmon::inotifyEventDiscoversDirectory(IN_CREATE)) {
        std::cerr << "FAIL: file CREATE was misclassified as directory discovery\n";
        return 1;
    }
    std::cout << "PASS: overflow/recovery policy and inotify record bounds\n";
    return 0;
}
