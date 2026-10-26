#include <sys/stat.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <ctime>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {

const fs::path root = fs::current_path();
const fs::path binary = root / "reminder_daemon";
const fs::path addEvent = root / "add_event";

class TestCase {
public:
    TestCase() {
        std::string pattern = "/tmp/reminder-test-XXXXXX";
        char* directory = ::mkdtemp(pattern.data());
        if (directory == nullptr) {
            throw std::runtime_error("mkdtemp failed");
        }
        workdir_ = directory;
        pidFile_ = workdir_ / "reminder.pid";
        configFile_ = workdir_ / "config.conf";
        eventsLog_ = workdir_ / "terminal_calls.log";
        fakeBin_ = workdir_ / "bin";
        fs::create_directory(fakeBin_);

        const fs::path fakeTerminal = fakeBin_ / "xterm";
        writeFile(fakeTerminal,
                  "#!/bin/sh\n"
                  "for item in \"$@\"; do last=\"$item\"; done\n"
                  "printf '%s\\n' \"$last\" >> \"$REMINDER_TEST_LOG\"\n");
        if (::chmod(fakeTerminal.c_str(), 0755) != 0) {
            throw std::runtime_error("chmod failed");
        }
    }

    ~TestCase() {
        stopDaemons();
        std::error_code error;
        fs::remove_all(workdir_, error);
    }

    void writeConfig(const std::string& events = "", int interval = 1) {
        writeFile(configFile_, "interval=" + std::to_string(interval) +
                                  "\nstate_dir=./state\nterminal=xterm\n" + events);
    }

    pid_t startDaemon(const fs::path& executable = binary) {
        const std::string command = "cd " + shellQuote(workdir_.string()) + " && " +
                                    environment() + " " + shellQuote(executable.string()) +
                                    " > /dev/null 2>&1";
        const int result = std::system(command.c_str());
        check(result == 0, "daemon failed to start");

        waitFor([this] { return runningPid().has_value(); },
                "daemon did not create PID file");
        const pid_t pid = *runningPid();
        check(fs::exists("/proc/" + std::to_string(pid)),
              "daemon process does not exist");
        pids_.push_back(pid);
        return pid;
    }

    void stopDaemons() {
        for (const pid_t pid : pids_) {
            if (::kill(pid, SIGTERM) == 0 || errno != ESRCH) {
                waitFor([pid] {
                    return !fs::exists("/proc/" + std::to_string(pid));
                }, "daemon did not stop");
            }
        }
        pids_.clear();
    }

    void testOnceAndShutdown() {
        const std::string minute = currentDateTime();
        writeConfig("event " + minute + " Initial reminder\n");
        const pid_t daemonPid = startDaemon();
        waitFor([this] { return countMessage("Initial reminder") > 0; },
                "initial event did not open a mock terminal");
        std::this_thread::sleep_for(2s);
        check(countMessage("Initial reminder") == 1,
              "initial event fired more than once");
        check(::kill(daemonPid, SIGTERM) == 0, "could not stop daemon");
        waitFor([this] { return !runningPid().has_value(); },
                "daemon did not clear PID after SIGTERM");
    }

    void testAllRepeatFlags() {
        const std::string minute = currentDateTime();
        const std::string date = minute.substr(0, 10);
        const std::string time = minute.substr(11);
        writeConfig("event " + date + " " + time + " -h Hourly event\n"
                    "event " + date + " " + time + " -d Daily event\n"
                    "event " + date + " " + time + " -w Weekly event\n");
        startDaemon();
        for (const std::string message : {"Hourly event", "Daily event", "Weekly event"}) {
            waitFor([this, &message] { return countMessage(message) > 0; },
                    "missing repeated reminder: " + message);
        }
        std::this_thread::sleep_for(1300ms);
        for (const std::string message : {"Hourly event", "Daily event", "Weekly event"}) {
            check(countMessage(message) == 1, "repeated event fired more than once");
        }
    }

    void testReloadThroughAddEvent() {
        writeConfig();
        startDaemon();
        const std::string minute = currentDateTime();
        const std::string command = "cd " + shellQuote(workdir_.string()) + " && " +
                                    environment() + " " + shellQuote(addEvent.string()) +
                                    " " + minute.substr(0, 10) + " " +
                                    minute.substr(11) + " After reload > /dev/null";
        check(std::system(command.c_str()) == 0, "add_event failed");
        waitFor([this] { return countMessage("After reload") > 0; },
                "daemon did not reload event added by add_event");
        check(countMessage("After reload") == 1, "reloaded event fired more than once");
    }

private:
    fs::path workdir_;
    fs::path pidFile_;
    fs::path configFile_;
    fs::path eventsLog_;
    fs::path fakeBin_;
    std::vector<pid_t> pids_;

    static void check(bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    static void writeFile(const fs::path& path, const std::string& content) {
        std::ofstream output(path);
        if (!output) {
            throw std::runtime_error("cannot write " + path.string());
        }
        output << content;
    }

    static std::string readFile(const fs::path& path) {
        std::ifstream input(path);
        std::ostringstream content;
        content << input.rdbuf();
        return content.str();
    }

    static std::string shellQuote(const std::string& value) {
        std::string result = "'";
        for (const char character : value) {
            if (character == '\'') {
                result += "'\\''";
            } else {
                result += character;
            }
        }
        return result + "'";
    }

    std::string environment() const {
        const char* oldPath = std::getenv("PATH");
        return "PATH=" + shellQuote(fakeBin_.string() + ":" +
                                    (oldPath == nullptr ? "" : oldPath)) +
               " DISPLAY=:99 REMINDER_TEST_LOG=" + shellQuote(eventsLog_.string()) +
               " REMINDER_CONFIG=" + shellQuote(configFile_.string());
    }

    std::optional<pid_t> runningPid() const {
        std::ifstream input(pidFile_);
        long value = 0;
        input >> value;
        if (!input || value <= 0) {
            return std::nullopt;
        }
        return static_cast<pid_t>(value);
    }

    int countMessage(const std::string& message) const {
        int count = 0;
        std::istringstream lines(readFile(eventsLog_));
        std::string line;
        while (std::getline(lines, line)) {
            if (line == message) {
                ++count;
            }
        }
        return count;
    }

    static std::string currentDateTime() {
        const std::time_t now = std::time(nullptr);
        std::tm local = {};
        ::localtime_r(&now, &local);
        char result[17] = {};
        if (std::strftime(result, sizeof(result), "%Y-%m-%d %H:%M", &local) == 0) {
            throw std::runtime_error("could not format current time");
        }
        return result;
    }

    static void waitFor(const std::function<bool()>& predicate, const std::string& message) {
        const auto deadline = std::chrono::steady_clock::now() + 7s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (predicate()) {
                return;
            }
            std::this_thread::sleep_for(80ms);
        }
        throw std::runtime_error(message);
    }
};

} // namespace

int main() {
    const std::vector<std::pair<std::string, void (TestCase::*)()>> tests = {
        {"once and shutdown", &TestCase::testOnceAndShutdown},
        {"repeat flags", &TestCase::testAllRepeatFlags},
        {"reload through add_event", &TestCase::testReloadThroughAddEvent},
    };

    try {
        for (const auto& [name, test] : tests) {
            TestCase instance;
            (instance.*test)();
            std::cout << "PASS: " << name << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
