#include "pid_file.h"
#include "logger.h"

#include <array>
#include <charconv>
#include <chrono>
#include <csignal>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <poll.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>

namespace {
    using Clock = std::chrono::steady_clock;

    struct Process {
        pid_t pid = 0;
        unsigned long long start = 0;
        unsigned long long device = 0;
        unsigned long long inode = 0;
        char state = 0;
        uid_t owner = 0;
        std::filesystem::path executable;
    };

    bool number(const std::string& text, unsigned long long& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    bool parse_record(const std::string& text, Process& process) {
        std::istringstream input(text);
        std::array<std::string, 4> fields;
        for (auto& field : fields) {
            if (!(input >> field)) {
                return false;
            }
        }
        std::string extra;
        unsigned long long pid = 0;
        if (input >> extra || !number(fields[0], pid) || pid <= 1 ||
            pid > static_cast<unsigned long long>(std::numeric_limits<pid_t>::max()) ||
            !number(fields[1], process.start) || process.start == 0 ||
            !number(fields[2], process.device) || !number(fields[3], process.inode)) {
            return false;
        }
        process.pid = static_cast<pid_t>(pid);
        return true;
    }

    bool inspect(pid_t pid, Process& process) {
        const auto path = std::filesystem::path("/proc") / std::to_string(pid);
        struct stat info{};
        if (stat(path.c_str(), &info) != 0) {
            if (errno == ENOENT || errno == ESRCH) {
                return false;
            }
            throw std::system_error(errno, std::generic_category(), "Cannot inspect process");
        }
        process.owner = info.st_uid;
        if (process.owner != geteuid()) {
            return false;
        }
        std::ifstream file(path / "stat");
        std::string text;
        if (!std::getline(file, text)) {
            if (!std::filesystem::exists(path)) {
                return false;
            }
            throw std::runtime_error("Cannot read process: " + std::to_string(pid));
        }
        const auto end = text.rfind(')');
        if (end == std::string::npos) {
            throw std::runtime_error("Invalid process data");
        }
        std::istringstream fields(text.substr(end + 1));
        std::string field;
        for (int index = 3; index <= 22; ++index) {
            if (!(fields >> field)) {
                throw std::runtime_error("Invalid process data");
            }
            if (index == 3) {
                process.state = field[0];
            }
        }
        if (!number(field, process.start)) {
            throw std::runtime_error("Invalid process data");
        }
        if (process.state == 'Z' || process.state == 'X') {
            return false;
        }
        std::error_code error;
        auto executable = std::filesystem::read_symlink(path / "exe", error).string();
        if (error) {
            if (error == std::errc::no_such_file_or_directory) {
                return false;
            }
            throw std::system_error(error, "Cannot read executable");
        }
        const std::string deleted = " (deleted)";
        if (executable.size() >= deleted.size() &&
            executable.compare(executable.size() - deleted.size(), deleted.size(), deleted) == 0) {
            executable.resize(executable.size() - deleted.size());
        }
        process.executable = executable;
        if (stat((path / "exe").c_str(), &info) != 0) {
            if (errno == ENOENT || errno == ESRCH) {
                return false;
            }
            throw std::system_error(errno, std::generic_category(), "Cannot inspect executable");
        }
        process.device = info.st_dev;
        process.inode = info.st_ino;
        process.pid = pid;
        return true;
    }

    void check_file(int fd) {
        struct stat info{};
        if (fstat(fd, &info) != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot inspect state file");
        }
        if (!S_ISREG(info.st_mode) || info.st_uid != geteuid() || info.st_nlink != 1) {
            throw std::runtime_error("Invalid state file");
        }
    }

    void write_all(int fd, const std::string& text) {
        std::size_t offset = 0;
        while (offset < text.size()) {
            const auto size = write(fd, text.data() + offset, text.size() - offset);
            if (size < 0 && errno == EINTR) {
                continue;
            }
            if (size <= 0) {
                throw std::system_error(size < 0 ? errno : EIO, std::generic_category(), "Cannot write PID");
            }
            offset += static_cast<std::size_t>(size);
        }
    }
}

PidFile::PidFile(const std::filesystem::path& directory) : directory(directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::system_error(error, "Cannot create state directory: " + directory.string());
    }
    dir.reset(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (dir.get() < 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot open state directory: " + directory.string());
    }
    dir.move_up();
    struct stat info{};
    if (fstat(dir.get(), &info) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot inspect state directory");
    }
    if (info.st_uid != geteuid()) {
        throw std::runtime_error("Invalid state owner");
    }
    if (fchmod(dir.get(), 0700) != 0 || faccessat(dir.get(), ".", W_OK | X_OK, 0) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot access state directory");
    }
    lock.reset(openat(dir.get(), "disk_monitor.lock", O_RDWR | O_CREAT | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (lock.get() < 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot open lock file");
    }
    lock.move_up();
    check_file(lock.get());
    if (fchmod(lock.get(), 0600) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot set lock permissions");
    }
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno != EWOULDBLOCK && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "Cannot lock monitor");
        }
        if (Clock::now() >= deadline) {
            throw std::runtime_error("Monitor busy");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

PidFile::~PidFile() {
    if (owns_pid) {
        try {
            remove_pid();
        } catch (const std::exception& error) {
            Logger::error(error.what());
        }
    }
}

std::string PidFile::read_pid() const {
    Fd file(openat(dir.get(), "disk_monitor.pid", O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW));
    if (file.get() < 0) {
        if (errno == ENOENT) {
            return {};
        }
        throw std::system_error(errno, std::generic_category(), "Cannot read PID file");
    }
    check_file(file.get());
    std::array<char, 257> buffer;
    std::size_t offset = 0;
    while (offset < buffer.size()) {
        const auto size = read(file.get(), buffer.data() + offset, buffer.size() - offset);
        if (size < 0 && errno == EINTR) {
            continue;
        }
        if (size < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot read PID file");
        }
        if (size == 0) {
            break;
        }
        offset += static_cast<std::size_t>(size);
    }
    if (offset == buffer.size()) {
        return "invalid";
    }
    return {buffer.data(), offset};
}

void PidFile::stop_previous() {
    Process saved;
    const auto text = read_pid();
    if (text.empty()) {
        return;
    }
    if (!parse_record(text, saved)) {
        Logger::info("Invalid PID file");
        return;
    }
    if (saved.pid == getpid()) {
        Logger::info("Stale PID file");
        return;
    }
    Fd process(static_cast<int>(syscall(SYS_pidfd_open, saved.pid, 0)));
    if (process.get() < 0) {
        if (errno == ESRCH) {
            Logger::info("Stale PID file");
            return;
        }
        throw std::system_error(errno, std::generic_category(), "Cannot open process");
    }
    Process current;
    Process self;
    if (!inspect(saved.pid, current) || current.owner != geteuid() ||
        current.start != saved.start || current.device != saved.device || current.inode != saved.inode ||
        !inspect(getpid(), self) || current.executable != self.executable) {
        Logger::info("Stale PID file");
        return;
    }
    if (syscall(SYS_pidfd_send_signal, process.get(), SIGTERM, nullptr, 0) != 0) {
        if (errno == ESRCH) {
            return;
        }
        throw std::system_error(errno, std::generic_category(), "Cannot stop monitor");
    }
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    pollfd descriptor{process.get(), POLLIN, 0};
    while (Clock::now() < deadline) {
        const int ready = poll(&descriptor, 1, 100);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP))) {
            Logger::info("Previous monitor stopped");
            return;
        }
        if (ready < 0 && errno != EINTR) {
            throw std::system_error(errno, std::generic_category(), "Cannot wait for monitor");
        }
        if (ready > 0 && (descriptor.revents & (POLLERR | POLLNVAL))) {
            throw std::runtime_error("Cannot wait for monitor");
        }
    }
    throw std::runtime_error("Cannot stop monitor: timeout");
}

void PidFile::save() {
    Process self;
    if (!inspect(getpid(), self)) {
        throw std::runtime_error("Cannot inspect monitor");
    }
    record = std::to_string(self.pid) + ' ' + std::to_string(self.start) + ' ' +
             std::to_string(self.device) + ' ' + std::to_string(self.inode) + '\n';
    const auto name = ".disk_monitor.pid." + std::to_string(getpid());
    Fd file(openat(dir.get(), name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (file.get() < 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot create PID file");
    }
    try {
        write_all(file.get(), record);
        if (fsync(file.get()) != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot save PID file");
        }
        if (renameat(dir.get(), name.c_str(), dir.get(), "disk_monitor.pid") != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot replace PID file");
        }
        owns_pid = true;
    } catch (...) {
        unlinkat(dir.get(), name.c_str(), 0);
        throw;
    }
}

void PidFile::remove_pid() {
    if (read_pid() == record && unlinkat(dir.get(), "disk_monitor.pid", 0) != 0 && errno != ENOENT) {
        throw std::system_error(errno, std::generic_category(), "Cannot remove PID file");
    }
}

void PidFile::release_lock() { lock.reset(); }
int PidFile::directory_fd() const { return dir.get(); }
int PidFile::lock_fd() const { return lock.get(); }
