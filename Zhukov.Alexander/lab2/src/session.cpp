#include "session.h"
#include "arguments.h"

#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>

namespace {

Fd open_process(pid_t pid) {
    Fd fd(static_cast<int>(syscall(SYS_pidfd_open, pid, 0)));
    if (fd.get() < 0) {
        system_error("Cannot open process");
    }
    return fd;
}

}

void Session::block_signals() {
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGUSR1);
    int error = pthread_sigmask(SIG_BLOCK, &signal_set, nullptr);
    if (error != 0) {
        errno = error;
        system_error("Cannot block SIGUSR1");
    }
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, nullptr) != 0) {
        system_error("Cannot ignore SIGPIPE");
    }
}

std::string Session::path(pid_t pid, const std::string& type) {
    return "/tmp/os-lab2-" + std::to_string(getuid()) + "-" + std::to_string(pid) + "-" + type;
}

std::string Session::semaphore_name(pid_t pid, const std::string& type, const std::string& suffix) {
    return "/os_lab2_" + std::to_string(getuid()) + "_" + std::to_string(pid) + "_" + type + "_" + suffix;
}

Session::Session(std::unique_ptr<Conn> connection, pid_t pid, bool create)
    : conn(std::move(connection)), host_pid(pid), host(create), directory(path(pid, conn->type())) {
    if (host_pid <= 0 || (host && host_pid != getpid())) {
        throw std::runtime_error("Invalid host PID");
    }
    try {
        if (host) {
            if (mkdir(directory.c_str(), 0700) != 0) {
                system_error("Cannot create session directory");
            }
            owner = true;
        } else {
            peer_fd = open_process(host_pid);
        }
        dir.reset(open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (dir.get() < 0) {
            system_error("Cannot open session directory");
        }
        struct stat info{};
        if (fstat(dir.get(), &info) != 0) {
            system_error("Cannot check session directory");
        }
        if (info.st_uid != getuid() || (info.st_mode & 0777) != 0700) {
            throw std::runtime_error("Invalid session directory");
        }
        if (!host) {
            auto expected = "os-lab2:1:" + conn->type() + ":" + std::to_string(host_pid);
            if (read_marker("ready") != expected || !process_alive(peer_fd.get())) {
                throw std::runtime_error("Host not ready");
            }
        }
        auto name = [&](const char* suffix) { return semaphore_name(host_pid, conn->type(), suffix); };
        slot.open(name("slot"), host, 1);
        accepted.open(name("accepted"), host);
        to_host.open(name("up"), host);
        to_client.open(name("down"), host);
        if (host) {
            conn->open(directory, true);
            sigset_t signal_set;
            sigemptyset(&signal_set);
            sigaddset(&signal_set, SIGUSR1);
            timespec delay{};
            while (sigtimedwait(&signal_set, nullptr, &delay) >= 0) {}
            write_marker("ready", "os-lab2:1:" + conn->type() + ":" + std::to_string(host_pid));
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

Session::~Session() { cleanup(); }

void Session::cleanup() noexcept {
    conn.reset();
    if (!host && claimed && !connected) {
        if (request_created) {
            unlinkat(dir.get(), "request", 0);
        }
        slot.post_on_exit();
        claimed = false;
    }
    if (owner) {
        for (const auto* name : {"ready", "request", "reply"}) {
            unlinkat(dir.get(), name, 0);
        }
        rmdir(directory.c_str());
        owner = false;
    }
}

void Session::write_marker(const char* name, const std::string& text) {
    std::string temporary = std::string(name) + ".new";
    Fd fd(openat(dir.get(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
    if (fd.get() < 0) {
        system_error("Cannot create session file");
    }
    ssize_t count;
    do {
        count = write(fd.get(), text.data(), text.size());
    } while (count < 0 && errno == EINTR);
    if (count != static_cast<ssize_t>(text.size())) {
        unlinkat(dir.get(), temporary.c_str(), 0);
        throw std::runtime_error("Cannot write session file");
    }
    if (linkat(dir.get(), temporary.c_str(), dir.get(), name, 0) != 0) {
        int error = errno;
        unlinkat(dir.get(), temporary.c_str(), 0);
        errno = error;
        system_error("Cannot publish session file");
    }
    if (unlinkat(dir.get(), temporary.c_str(), 0) != 0) {
        system_error("Cannot remove session temporary file");
    }
}

std::string Session::read_marker(const char* name) {
    Fd fd(openat(dir.get(), name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC));
    if (fd.get() < 0) {
        system_error("Cannot read session file");
    }
    struct stat info{};
    if (fstat(fd.get(), &info) != 0) {
        system_error("Cannot check session file");
    }
    if (!S_ISREG(info.st_mode) || info.st_uid != getuid() ||
        (info.st_mode & 0777) != 0600 || info.st_size <= 0 || info.st_size > 128) {
        throw std::runtime_error("Invalid session file");
    }
    std::string text(static_cast<std::size_t>(info.st_size), '\0');
    ssize_t count;
    do {
        count = read(fd.get(), text.data(), text.size());
    } while (count < 0 && errno == EINTR);
    if (count != static_cast<ssize_t>(text.size())) {
        throw std::runtime_error("Cannot read session file");
    }
    return text;
}

void Session::connect(const std::atomic_bool& stop) {
    auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    check_stop(stop);
    if (!slot.try_wait()) {
        throw std::runtime_error("Host busy");
    }
    claimed = true;
    conn->open(directory, false);
    if (conn->peer_pid() != 0 && conn->peer_pid() != host_pid) {
        throw std::runtime_error("Invalid host");
    }
    write_marker("request", std::to_string(getpid()));
    request_created = true;
    if (syscall(SYS_pidfd_send_signal, peer_fd.get(), SIGUSR1, nullptr, 0) != 0) {
        system_error("Cannot signal host");
    }
    accepted.wait(deadline, stop, peer_fd.get());
    if (read_marker("reply") != std::to_string(getpid())) {
        throw std::runtime_error("Invalid handshake reply");
    }
    peer = host_pid;
    connected = true;
}

bool Session::accept(const std::atomic_bool& stop) {
    check_stop(stop);
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGUSR1);
    siginfo_t info{};
    timespec delay{0, poll_ms * 1000000L};
    int signal = sigtimedwait(&signal_set, &info, &delay);
    if (signal < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return false;
        }
        system_error("Cannot wait for handshake");
    }
    if (info.si_uid != getuid() || info.si_pid <= 0 || info.si_pid == host_pid) {
        return false;
    }
    std::string request;
    try {
        request = read_marker("request");
    } catch (const std::exception&) {
        return false;
    }
    if (request != std::to_string(info.si_pid)) {
        return false;
    }
    peer_fd = open_process(info.si_pid);
    conn->accept();
    if (conn->peer_pid() != 0 && conn->peer_pid() != info.si_pid) {
        throw std::runtime_error("Invalid client");
    }
    write_marker("reply", request);
    peer = info.si_pid;
    connected = true;
    accepted.post();
    return true;
}

Message Session::prepare(Message message) {
    if (!connected) {
        throw std::runtime_error("Not connected");
    }
    message.sender = host ? 0 : 1;
    message.number = ++number;
    if (message.sent_at == 0) {
        message.sent_at = std::chrono::duration_cast<std::chrono::nanoseconds>(
                              Clock::now().time_since_epoch()).count();
    }
    encode(message);
    return message;
}

void Session::transmit(const Message& message, const std::atomic_bool& stop) {
    conn->send(encode(message), stop);
    (host ? to_client : to_host).post();
}

Message Session::send(Message message, const std::atomic_bool& stop) {
    message = prepare(std::move(message));
    transmit(message, stop);
    return message;
}

void Session::forward(const Message& message, const std::atomic_bool& stop) {
    if (!host || !connected || message.sender != 1) {
        throw std::runtime_error("Invalid forwarded message");
    }
    transmit(message, stop);
}

void Session::kill_peer() {
    if (!host || !connected) {
        throw std::runtime_error("Not connected");
    }
    if (syscall(SYS_pidfd_send_signal, peer_fd.get(), SIGKILL, nullptr, 0) != 0 && errno != ESRCH) {
        system_error("Cannot stop client");
    }
}

ReadResult Session::receive(Message& message, const std::atomic_bool& stop, int wait_ms) {
    if (!connected) {
        throw std::runtime_error("Not connected");
    }
    Bytes packet;
    Deadline deadline;
    auto result = conn->receive(packet, stop, wait_ms, &deadline);
    if (result == ReadResult::NoData && !process_alive(peer_fd.get())) {
        result = conn->receive(packet, stop, 0, &deadline);
        if (result == ReadResult::NoData) {
            return ReadResult::Closed;
        }
    }
    if (result == ReadResult::Data) {
        (host ? to_host : to_client).wait(deadline, stop, peer_fd.get());
        message = decode(packet);
        if (host && message.sender != 1) {
            throw std::runtime_error("Invalid sender");
        }
    }
    return result;
}
