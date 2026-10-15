#include "Config.h"
#include "EventFormatter.h"
#include "PidFile.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void testConfigParsing() {
    char directory_template[] = "/tmp/disk-monitor-unit.XXXXXX";
    const char* created_directory = mkdtemp(directory_template);
    assert(created_directory != nullptr);

    const std::filesystem::path directory(created_directory);
    const auto config_path = directory / "monitor.conf";
    const auto first = directory / "first";
    const auto second = directory / "second";
    std::filesystem::create_directories(first);
    std::filesystem::create_directories(second);

    std::ofstream config(config_path);
    config << "# comment\n"
           << "DIR=first   # inline comment\n"
           << "second\n"
           << "first\r\n"
           << "\n";
    config.close();

    const auto parsed = disk_monitor::ConfigParser::load(config_path);
    assert(parsed.file == std::filesystem::absolute(config_path));
    assert(parsed.directories.size() == 2);
    assert(parsed.directories[0] == std::filesystem::weakly_canonical(first));
    assert(parsed.directories[1] == std::filesystem::weakly_canonical(second));

    std::filesystem::remove_all(directory);
}

void testEventFormatting() {
    const auto text = disk_monitor::eventMaskToText(IN_CREATE | IN_ISDIR | IN_MOVED_TO);
    assert(text.find("CREATE") != std::string::npos);
    assert(text.find("MOVED_TO") != std::string::npos);
    assert(disk_monitor::eventKindToText(IN_ISDIR) == "directory");
    assert(disk_monitor::eventKindToText(IN_MODIFY) == "file");
}

void testPidProcessCheck() {
    assert(disk_monitor::PidFile::processExists(getpid()));
    assert(!disk_monitor::PidFile::processExists(-1));
    assert(!disk_monitor::PidFile::processExists(999999999));
}

}  // namespace

int main() {
    testConfigParsing();
    testEventFormatting();
    testPidProcessCheck();
    std::cout << "All unit tests passed\n";
    return 0;
}

