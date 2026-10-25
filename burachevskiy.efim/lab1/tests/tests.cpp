#include "events.h"
#include "daemon.h"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <pty.h>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
fs::path root;
fs::path binary;
fs::path testBinary;
std::vector<pid_t> children;
int passed = 0;
int failed = 0;
int fixtureNumber = 0;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const fs::path& path, const std::string& text) {
    std::ofstream output(path);
    output << text;
    output.close();
    require(!output.fail(), "Cannot write " + path.string());
}

void waitFor(const std::function<bool()>& condition, const std::string& message, int milliseconds = 6000) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
    while (Clock::now() < deadline) {
        if (condition()) return;
        std::this_thread::sleep_for(20ms);
    }
    throw std::runtime_error(message);
}

bool running(pid_t pid) {
    if (pid <= 1 || kill(pid, 0) == -1) return false;
    const auto data = read("/proc/" + std::to_string(pid) + "/stat");
    const auto end = data.rfind(')');
    return end != std::string::npos && data.size() > end + 2 && data[end + 2] != 'Z';
}

void stop(pid_t pid) {
    if (running(pid)) require(kill(pid, SIGTERM) == 0, "SIGTERM failed");
    waitFor([&] { return !running(pid); }, "Process did not stop");
    int status;
    waitpid(pid, &status, WNOHANG);
}

pid_t spawn(const std::vector<std::string>& args, const std::map<std::string, std::string>& environment,
            const fs::path& output, bool terminal = false) {
    int master = -1, slave = -1;
    if (terminal) require(openpty(&master, &slave, nullptr, nullptr, nullptr) == 0, "openpty");
    const pid_t child = fork();
    require(child != -1, "fork");
    if (child == 0) {
        if (terminal) {
            close(master);
            if (setsid() == -1 || ioctl(slave, TIOCSCTTY, 0) == -1) _exit(120);
        }
        const int nullFd = open("/dev/null", O_RDONLY);
        const int log = open(output.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
        if (nullFd == -1 || log == -1) _exit(121);
        if (dup2(terminal ? slave : nullFd, STDIN_FILENO) == -1 ||
            dup2(terminal ? slave : log, STDOUT_FILENO) == -1 ||
            dup2(terminal ? slave : log, STDERR_FILENO) == -1) _exit(122);
        close(nullFd);
        close(log);
        if (slave > 2) close(slave);
        for (const auto& [key, value] : environment) if (setenv(key.c_str(), value.c_str(), 1) == -1) _exit(123);
        std::vector<char*> argv;
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(args[0].c_str(), argv.data());
        _exit(127);
    }
    if (terminal) close(slave);
    children.push_back(child);
    if (terminal) {
        waitFor([&] { return !running(child); }, "Launcher with TTY did not finish", 15000);
        close(master);
    }
    return child;
}

int finish(pid_t child) {
    int status = 0;
    waitFor([&] {
        const pid_t result = waitpid(child, &status, WNOHANG);
        if (result == -1) throw std::runtime_error("waitpid failed");
        return result == child;
    }, "Command timeout", 15000);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

std::string datePart(std::time_t time, const char* format) {
    std::tm value{};
    require(localtime_r(&time, &value) != nullptr, "localtime_r");
    std::ostringstream out;
    out << std::put_time(&value, format);
    return out.str();
}

struct Fixture {
    fs::path folder, config, terminal, log, failure;
    Fixture() {
        folder = root / std::to_string(++fixtureNumber);
        fs::create_directories(folder / "terminal with spaces");
        config = folder / "events.conf";
        terminal = folder / "terminal with spaces" / "ptyxis";
        log = folder / "attempts.log";
        failure = folder / "terminal.fail";
        fs::create_symlink(testBinary, terminal);
        settings();
    }
    std::string header(int interval = 1) const {
        return "interval=" + std::to_string(interval) + "\nterminal=" + terminal.string() + "\n";
    }
    void settings(int interval = 1, const std::string& events = "") { write(config, header(interval) + events); }
    std::map<std::string, std::string> env() const {
        return {{"DISPLAY", "test-session"}, {"REMINDER_TEST_LOG", log.string()},
                {"REMINDER_TEST_FAIL", failure.string()}, {"REMINDER_CONFIG", config.string()}};
    }
    int run(const std::vector<std::string>& args, const fs::path& program = {}) const {
        std::vector<std::string> command{program.empty() ? binary.string() : program.string()};
        command.insert(command.end(), args.begin(), args.end());
        return finish(spawn(command, env(), folder / "command.log"));
    }
    pid_t pid() const {
        std::istringstream input(read(config.string() + ".pid"));
        pid_t value = 0;
        input >> value;
        return value;
    }
    pid_t start(bool foreground = false, const fs::path& program = {}, bool tty = false) const {
        const auto path = program.empty() ? binary : program;
        std::vector<std::string> command{path.string()};
        if (foreground) command.push_back("--foreground");
        command.push_back(config.string());
        const pid_t launcher = spawn(command, env(), folder / "daemon.log", tty);
        if (!foreground) require(finish(launcher) == 0, "Daemon launch failed: " + read(folder / "daemon.log"));
        waitFor([&] { return pid() > 1 && running(pid()); }, "PID file not ready");
        children.push_back(pid());
        return pid();
    }
    void add(std::time_t time, const std::string& text, const std::string& flag = "") const {
        std::vector<std::string> args{"--add", config.string(), datePart(time, "%F"), datePart(time, "%T")};
        if (!flag.empty()) args.push_back(flag);
        args.push_back(text);
        require(run(args) == 0, "Add failed: " + read(folder / "command.log"));
    }
    int count(const std::string& prefix = "OK ") const {
        int result = 0;
        std::istringstream input(read(log));
        std::string line;
        while (std::getline(input, line)) if (line.rfind(prefix, 0) == 0) ++result;
        return result;
    }
    void delivered(int expected = 1) const {
        waitFor([&] { return count() >= expected && !read(config.string() + ".state").empty(); }, "Reminder not acknowledged: " + read(folder / "daemon.log"));
    }
};

void test(const std::string& name, const std::function<void()>& body) {
    try { body(); ++passed; std::cout << "PASS " << name << std::endl; }
    catch (const std::exception& error) { ++failed; std::cerr << "FAIL " << name << ": " << error.what() << std::endl; }
}

void rejects(const std::function<void()>& body) {
    bool rejected = false;
    try { body(); } catch (const std::exception&) { rejected = true; }
    require(rejected, "Expected an error");
}

int fakeTerminal(int argc, char* argv[]) {
    const char* log = std::getenv("REMINDER_TEST_LOG");
    const char* failure = std::getenv("REMINDER_TEST_FAIL");
    if (!log || !failure) return 100;
    const bool deferred = read(failure) == "defer";
    const bool failing = fs::exists(failure) && !deferred;
    const int fd = open(log, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd == -1 || flock(fd, LOCK_EX) == -1) return 101;
    std::ostringstream line;
    line << (failing ? "FAIL " : "OK ");
    for (int i = 1; i < argc; ++i) line << std::quoted(argv[i]) << ' ';
    line << '\n';
    const std::string data = line.str();
    const auto written = ::write(fd, data.data(), data.size());
    close(fd);
    if (written != static_cast<ssize_t>(data.size())) return 102;
    if (failing) return 42;
    if (argc != 8 || std::string(argv[1]) != "--new-window" || std::string(argv[2]) != "--" ||
        std::string(argv[4]) != "--show" || std::string(argv[6]) != "--ack-file") return 103;
    if (deferred) waitFor([&] { return !fs::exists(failure); }, "Deferred terminal was not released");
    const int nullFd = open("/dev/null", O_RDWR);
    if (nullFd == -1) return 104;
    for (int target : {0, 1, 2}) if (dup2(nullFd, target) == -1) return 105;
    if (nullFd > 2) close(nullFd);
    execv(argv[3], argv + 3);
    return 127;
}

std::string ticks(pid_t pid) {
    const auto data = read("/proc/" + std::to_string(pid) + "/stat");
    std::istringstream input(data.substr(data.rfind(')') + 2));
    std::string field;
    for (int i = 3; i <= 22; ++i) input >> field;
    return field;
}
}

int main(int argc, char* argv[]) {
    if (fs::path(argv[0]).filename() == "ptyxis") return fakeTerminal(argc, argv);
    if (argc != 2) { std::cerr << "Usage: tests PATH_TO_REMINDER\n"; return 2; }
    if (prctl(PR_SET_CHILD_SUBREAPER, 1) == -1) { perror("prctl"); return 2; }
    setenv("TZ", "UTC", 1);
    tzset();
    binary = fs::canonical(argv[1]);
    testBinary = fs::read_symlink("/proc/self/exe");
    char pattern[] = "/tmp/reminder-tests.XXXXXX";
    const char* directory = mkdtemp(pattern);
    if (!directory) { perror("mkdtemp"); return 2; }
    root = directory;

    test("Singleton returns the same object", [] { require(&Daemon::instance() == &Daemon::instance(), "Different instances"); });
    test("valid date and optional seconds", [] { require(parseDate("2026-10-25", "18:30") == parseDate("2026-10-25", "18:30:00"), "Different timestamps"); });
    test("leap day", [] { require(parseDate("2024-02-29", "00:00") + 86400 == parseDate("2024-03-01", "00:00"), "Leap year"); });
    for (const auto& value : std::vector<std::pair<std::string, std::string>>{{"2026-02-30", "12:00"}, {"2025-02-29", "12:00"},
            {"2026-13-01", "12:00"}, {"2026-01-01", "24:00"}, {"2026-01-01", "12:60"}, {"2026-01-01", "12:00:60"},
            {"25.10.2026", "12:00"}, {"2026-1-01", "12:00"}, {"1969-12-31", "12:00"}}) {
        test("reject date " + value.first + " " + value.second, [&] { rejects([&] { parseDate(value.first, value.second); }); });
    }
    test("one-time event keeps its time", [] {
        const Event event{"one", parseDate("2026-10-25", "10:30"), '-', "text"};
        require(latestOccurrence(event, event.start + 9000) == event.start, "Changed one-time event");
        require(latestOccurrence(event, event.start - 1) > event.start - 1, "Future event became due");
    });
    test("hourly repeat and missed periods", [] {
        const Event event{"hour", parseDate("2026-10-25", "10:30"), 'h', "text"};
        require(latestOccurrence(event, parseDate("2026-10-25", "13:45")) == parseDate("2026-10-25", "13:30"), "Hourly schedule");
    });
    test("daily repeat across month boundary", [] {
        const Event event{"day", parseDate("2026-01-31", "18:30"), 'd', "text"};
        require(latestOccurrence(event, parseDate("2026-02-02", "12:00")) == parseDate("2026-02-01", "18:30"), "Daily schedule");
    });
    test("weekly repeat across year boundary", [] {
        const Event event{"week", parseDate("2025-12-29", "18:30"), 'w', "text"};
        require(latestOccurrence(event, parseDate("2026-01-06", "12:00")) == parseDate("2026-01-05", "18:30"), "Weekly schedule");
    });
    test("repeat does not precede its start", [] {
        const auto start = parseDate("2026-10-25", "18:30");
        for (char repeat : {'h', 'd', 'w'}) require(latestOccurrence({"future", start, repeat, "text"}, start - 1) == start, "Before start");
    });
    test("daily repeat keeps local hour over DST", [] {
        setenv("TZ", "Europe/Berlin", 1); tzset();
        const Event event{"dst", parseDate("2026-03-28", "12:00"), 'd', "text"};
        const auto result = latestOccurrence(event, parseDate("2026-03-29", "13:00"));
        const auto expected = parseDate("2026-03-29", "12:00");
        setenv("TZ", "UTC", 1); tzset();
        require(result == expected && result - event.start == 23 * 3600, "DST local hour");
    });
    test("configuration comments, CRLF and duplicate identity", [] {
        Fixture f;
        write(f.config, "# comment\r\ninterval=10\r\nterminal=auto\r\n2026-10-25 18:30 - \"text\"\r\n2026-10-25 18:30 - \"text\"\r\n");
        const auto value = readConfig(f.config);
        require(value.interval == 10 && value.events.size() == 2 && value.events[0].key != value.events[1].key, "Config values");
    });
    for (const auto& line : {"interval=0", "interval=-1", "interval=3601", "interval=1x", "interval=1\ninterval=2", "terminal=", "unknown=1", "2026-10-25 18:30 -x \"text\"", "2026-10-25 18:30 - text", "2026-10-25 18:30 - \"\"", "2026-10-25 18:30 - \"text\" trailing"}) {
        test("reject configuration " + std::string(line), [&] { Fixture f; write(f.config, line); rejects([&] { readConfig(f.config); }); });
    }
    test("add_event script preserves literal text and all repeat flags", [] {
        Fixture f;
        const std::string text = "Русский текст; $(touch SHOULD_NOT_EXIST) \\\"quoted\\\"";
        const auto script = binary.parent_path() / "add_event";
        for (const auto& flag : {"", "-h", "-d", "-w"}) {
            std::vector<std::string> args{"2026-10-25", "18:30"};
            if (*flag) args.push_back(flag);
            args.push_back(text);
            require(f.run(args, script) == 0, "Script failed");
        }
        const auto value = readConfig(f.config);
        require(value.events.size() == 4, "Wrong event count");
        for (const auto& event : value.events) require(event.text == text, "Text changed");
    });
    test("invalid add leaves config unchanged", [] {
        Fixture f;
        const auto before = read(f.config);
        require(f.run({"--add", f.config.string(), "2026-02-30", "18:30", "text"}) != 0, "Invalid add accepted");
        require(read(f.config) == before, "Config changed");
    });
    test("bad add flags and multiline text leave config unchanged", [] {
        Fixture f; const auto before = read(f.config);
        for (const auto& tail : std::vector<std::vector<std::string>>{{"-x", "text"}, {"-h"}, {""}, {"first\nsecond"}}) {
            std::vector<std::string> args{"--add", f.config.string(), "2026-10-25", "18:30"};
            args.insert(args.end(), tail.begin(), tail.end());
            require(f.run(args) != 0 && read(f.config) == before, "Invalid event changed config");
        }
    });
    test("concurrent add does not lose records", [] {
        Fixture f;
        std::vector<pid_t> writers;
        for (int i = 0; i < 8; ++i) writers.push_back(spawn({binary.string(), "--add", f.config.string(), "2026-10-25", "18:30", "event " + std::to_string(i)}, f.env(), f.folder / ("writer" + std::to_string(i))));
        for (auto pid : writers) require(finish(pid) == 0, "Concurrent writer failed");
        require(readConfig(f.config).events.size() == 8, "Lost records");
    });
    test("check valid and missing terminal", [] {
        Fixture f;
        require(f.run({"--check", f.config.string()}) == 0, "Valid check failed");
        fs::remove(f.terminal);
        require(f.run({"--check", f.config.string()}) != 0, "Missing terminal accepted");
    });
    test("daemon PID, cwd, streams, session and PID lock", [] {
        Fixture f;
        const pid_t pid = f.start();
        const fs::path proc = "/proc/" + std::to_string(pid);
        require(fs::read_symlink(proc / "cwd") == "/", "Wrong cwd");
        for (int fd : {0, 1, 2}) require(fs::read_symlink(proc / "fd" / std::to_string(fd)) == "/dev/null", "Terminal stream remains");
        const auto data = read(proc / "stat");
        std::istringstream input(data.substr(data.rfind(')') + 2));
        char state; long ppid, group, session, tty;
        input >> state >> ppid >> group >> session >> tty;
        require(tty == 0 && session != pid, "Controlling TTY or session leader remains");
        const int fd = open((f.config.string() + ".pid").c_str(), O_RDWR);
        require(fd != -1, "Open PID file");
        const bool locked = flock(fd, LOCK_EX | LOCK_NB) == -1 && errno == EWOULDBLOCK;
        close(fd);
        require(locked, "PID file is not locked");
        stop(pid);
        require(read(f.config.string() + ".pid").empty(), "PID file not cleared");
    });
    test("survives closing source pseudo-terminal", [] {
        Fixture f;
        const pid_t pid = f.start(false, {}, true);
        std::this_thread::sleep_for(200ms);
        require(running(pid), "Daemon died with source terminal");
        stop(pid);
    });
    test("daemon closes unrelated inherited descriptors", [] {
        Fixture f;
        const auto path = f.folder / "inherited.txt";
        const int inherited = open(path.c_str(), O_CREAT | O_RDWR, 0600);
        require(inherited != -1, "Open inherited descriptor");
        const auto pid = f.start();
        close(inherited);
        bool leaked = false;
        for (const auto& entry : fs::directory_iterator("/proc/" + std::to_string(pid) + "/fd")) {
            std::error_code error;
            if (fs::read_symlink(entry.path(), error) == path) leaked = true;
        }
        stop(pid);
        require(!leaked, "Inherited descriptor leaked");
    });
    test("one-time notification, literal arguments, state and no replay", [] {
        Fixture f;
        const std::string text = "Привет; $(touch " + (f.folder / "injected").string() + ") \"hello\"";
        f.add(std::time(nullptr) - 1, text);
        auto pid = f.start(); f.delivered();
        std::ostringstream quoted; quoted << std::quoted(text);
        require(read(f.log).find(quoted.str()) != std::string::npos, "Text was split or changed");
        require(!fs::exists(f.folder / "injected"), "Shell code executed");
        std::this_thread::sleep_for(1200ms);
        require(f.count() == 1, "Duplicate during same run");
        stop(pid);
        pid = f.start();
        std::this_thread::sleep_for(1200ms);
        require(f.count() == 1, "Duplicate after restart");
        stop(pid);
    });
    for (const auto& flag : {"-h", "-d", "-w"}) test("integration repeat " + std::string(flag), [&] {
        Fixture f; f.add(std::time(nullptr) - 10, flag, flag);
        const auto pid = f.start(); f.delivered(); stop(pid);
    });
    test("future event is not displayed early", [] {
        Fixture f; f.add(std::time(nullptr) + 3600, "future");
        const auto pid = f.start();
        std::this_thread::sleep_for(1200ms);
        require(f.count() == 0, "Future event displayed"); stop(pid);
    });
    test("add event while daemon runs", [] {
        Fixture f; const auto pid = f.start();
        f.add(std::time(nullptr) - 1, "added later"); f.delivered(); stop(pid);
    });
    test("SIGHUP wakes 60-second wait and applies new config", [] {
        Fixture f; f.settings(60); const auto pid = f.start();
        f.add(std::time(nullptr) - 1, "reload");
        require(kill(pid, SIGHUP) == 0, "SIGHUP"); f.delivered(); stop(pid);
    });
    test("bad reload preserves previous valid schedule", [] {
        Fixture f; f.add(std::time(nullptr) + 2, "keep old schedule"); const auto pid = f.start();
        write(f.config, "interval=broken\n"); require(kill(pid, SIGHUP) == 0, "SIGHUP");
        f.delivered(); require(running(pid), "Daemon died on reload"); stop(pid);
    });
    test("SIGTERM interrupts long wait and logs shutdown", [] {
        Fixture f; f.settings(60); const auto pid = f.start(true);
        const auto started = Clock::now(); stop(pid);
        require(Clock::now() - started < 2s, "Shutdown waited for interval");
        const auto log = read(f.folder / "daemon.log");
        require(log.find("Daemon started") != std::string::npos && log.find("Daemon stopped") != std::string::npos, "Lifecycle messages missing");
    });
    test("SIGTERM saves an acknowledgement already written by the display process", [] {
        Fixture f; write(f.failure, "defer"); f.add(std::time(nullptr) - 1, "confirmed before shutdown");
        auto pid = f.start(); waitFor([&] { return f.count() == 1; }, "Terminal not started");
        struct Resume {
            pid_t pid;
            ~Resume() { if (running(pid)) kill(pid, SIGCONT); }
        } resume{pid};
        require(kill(pid, SIGSTOP) == 0, "SIGSTOP");
        waitFor([&] {
            const auto stat = read("/proc/" + std::to_string(pid) + "/stat");
            const auto bracket = stat.rfind(')');
            return bracket != std::string::npos && stat[bracket + 2] == 'T';
        }, "Daemon did not pause");
        fs::remove(f.failure);
        waitFor([&] {
            for (const auto& entry : fs::directory_iterator(f.folder)) {
                if (entry.path().filename().string().rfind("events.conf.notify.", 0) == 0 && read(entry.path()) == "ready\n") return true;
            }
            return false;
        }, "Display did not acknowledge");
        require(kill(pid, SIGTERM) == 0 && kill(pid, SIGCONT) == 0, "Resume with pending SIGTERM");
        waitFor([&] { return !running(pid); }, "Daemon did not stop");
        require(!read(f.config.string() + ".state").empty(), "Shutdown lost ready acknowledgement");
        pid = f.start(); std::this_thread::sleep_for(1200ms);
        require(f.count() == 1, "Confirmed event replayed"); stop(pid);
    });
    test("busy configuration lock does not block SIGTERM", [] {
        Fixture f; const auto pid = f.start();
        const int fd = open((f.config.string() + ".lock").c_str(), O_RDWR | O_CLOEXEC);
        require(fd != -1 && flock(fd, LOCK_EX) == 0, "Lock config");
        std::this_thread::sleep_for(1200ms);
        const auto started = Clock::now(); stop(pid); close(fd);
        require(Clock::now() - started < 2s, "Blocked while reading config");
    });
    test("restart terminates previous daemon", [] {
        Fixture f; const auto old = f.start(); const auto current = f.start();
        require(old != current && !running(old) && running(current), "Replacement failed"); stop(current);
    });
    test("restart after atomic executable replacement", [] {
        Fixture f; const auto program = f.folder / "reminder";
        fs::copy_file(binary, program); const auto old = f.start(false, program);
        fs::copy_file(binary, f.folder / "new-reminder"); fs::rename(f.folder / "new-reminder", program);
        const auto current = f.start(false, program);
        require(old != current && !running(old), "Old inode prevented restart"); stop(current);
    });
    test("stale unlocked PID does not signal unrelated process", [] {
        Fixture f;
        const pid_t victim = fork(); require(victim != -1, "fork victim");
        if (victim == 0) { for (;;) pause(); }
        children.push_back(victim);
        write(f.config.string() + ".pid", std::to_string(victim) + "\n" + ticks(victim) + "\n\"" + f.config.string() + "\"\n");
        const auto pid = f.start(); require(running(victim), "Unrelated process killed"); stop(pid); stop(victim);
    });
    test("forged occupied PID does not signal unrelated process", [] {
        Fixture f;
        const pid_t victim = fork(); require(victim != -1, "fork victim");
        if (victim == 0) { for (;;) pause(); }
        children.push_back(victim);
        const auto path = f.config.string() + ".pid";
        write(path, std::to_string(victim) + "\n" + ticks(victim) + "\n\"" + f.config.string() + "\"\n");
        const int fd = open(path.c_str(), O_RDWR | O_CLOEXEC); require(fd != -1, "open forged PID");
        require(flock(fd, LOCK_EX) == 0, "lock forged PID");
        const int code = f.run({f.config.string()}); close(fd);
        require(code != 0 && running(victim), "Forged PID was trusted"); stop(victim);
    });
    test("invalid replacement config leaves old daemon alive", [] {
        Fixture f; const auto old = f.start(); write(f.config, "interval=wrong\n");
        require(f.run({f.config.string()}) != 0 && running(old), "Old daemon stopped before validation"); stop(old);
    });
    test("PID path cannot overwrite config", [] {
        Fixture f; const auto before = read(f.config);
        require(f.run({f.config.string(), "--pid-file", f.config.string()}) != 0 && read(f.config) == before, "Config overwritten");
    });
    test("PID path through a directory symlink cannot overwrite config", [] {
        Fixture f; const auto before = read(f.config);
        fs::create_directory_symlink(f.folder, f.folder / "alias");
        require(f.run({f.config.string(), "--pid-file", (f.folder / "alias" / "events.conf").string()}) != 0 && read(f.config) == before, "Config overwritten through alias");
    });
    test("terminal failure is retried without false delivery", [] {
        Fixture f; write(f.failure, "fail"); f.add(std::time(nullptr) - 1, "retry");
        const auto pid = f.start(); waitFor([&] { return f.count("FAIL ") > 0; }, "No launch attempt");
        require(read(f.config.string() + ".state").empty(), "Failed launch marked delivered");
        fs::remove(f.failure); f.delivered(); require(f.count() == 1, "Retry not delivered"); stop(pid);
    });
    test("corrupt state is rejected", [] {
        Fixture f; write(f.config.string() + ".state", "broken state");
        require(f.run({f.config.string()}) != 0, "Corrupt state accepted");
    });
    test("unknown arguments fail", [] { Fixture f; require(f.run({f.config.string(), "--unknown"}) != 0, "Unknown option accepted"); });

    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (running(*it)) kill(*it, SIGTERM);
    }
    const auto deadline = Clock::now() + 3s;
    while (Clock::now() < deadline) {
        int status;
        if (waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD) break;
        std::this_thread::sleep_for(20ms);
    }
    std::cout << "Result: " << passed << " passed, " << failed << " failed\n";
    if (failed == 0) fs::remove_all(root);
    else std::cout << "Diagnostics: " << root << '\n';
    return failed == 0 ? 0 : 1;
}
