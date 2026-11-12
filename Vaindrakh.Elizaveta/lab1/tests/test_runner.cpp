#include "Config.h"
#include "EventFormatter.h"
#include "PidFile.h"

#include <sys/inotify.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

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

    char directory_template[] = "/tmp/disk-monitor-pid.XXXXXX";
    const char* created_directory = mkdtemp(directory_template);
    assert(created_directory != nullptr);
    const std::filesystem::path directory(created_directory);
    const auto pid_path = directory / "monitor.pid";

    const pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        execl("/bin/sleep", "foreign-disk_monitor-helper", "30", nullptr);
        _exit(127);
    }

    bool child_has_expected_command_line = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        std::ifstream command_line("/proc/" + std::to_string(child) + "/cmdline");
        const std::string contents((std::istreambuf_iterator<char>(command_line)),
                                   std::istreambuf_iterator<char>());
        if (contents.find("foreign-disk_monitor-helper") != std::string::npos) {
            child_has_expected_command_line = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(child_has_expected_command_line);

    {
        std::ofstream pid_file(pid_path);
        pid_file << child << '\n';
    }

    disk_monitor::Logger logger;
    assert(!disk_monitor::PidFile::stopPrevious(pid_path, logger));
    assert(kill(child, 0) == 0);

    assert(kill(child, SIGKILL) == 0);
    assert(waitpid(child, nullptr, 0) == child);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    testConfigParsing();
    testEventFormatting();
    testPidProcessCheck();
    std::cout << "All unit tests passed\n";
    return 0;
}

