#include "wolf/client.hpp"
#include "wolf/host_window.hpp"
#include "wolf/shared_state.hpp"
#include "wolf/transport.hpp"

#include <QApplication>
#include <QObject>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

volatile std::sig_atomic_t g_signal_fd = -1;

void HandleSignal(int number) {
    const int saved_errno = errno;
    const int fd = static_cast<int>(g_signal_fd);
    if (fd >= 0) {
        const unsigned char byte = static_cast<unsigned char>(number);
        const ssize_t ignored = write(fd, &byte, sizeof(byte));
        (void)ignored;
    }
    errno = saved_errno;
}

class SignalPipe {
public:
    SignalPipe() {
        int descriptors[2] = {-1, -1};
        if (pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) != 0) {
            throw std::runtime_error("pipe2: " + std::string(std::strerror(errno)));
        }
        read_fd_ = descriptors[0];
        write_fd_ = descriptors[1];

        struct sigaction action {};
        action.sa_handler = HandleSignal;
        sigemptyset(&action.sa_mask);
        g_signal_fd = write_fd_;

        if (sigaction(SIGINT, &action, &old_int_) != 0) {
            const int error = errno;
            Close();
            throw std::runtime_error("sigaction(SIGINT): " + std::string(std::strerror(error)));
        }
        int_installed_ = true;

        if (sigaction(SIGTERM, &action, &old_term_) != 0) {
            const int error = errno;
            sigaction(SIGINT, &old_int_, nullptr);
            int_installed_ = false;
            Close();
            throw std::runtime_error("sigaction(SIGTERM): " + std::string(std::strerror(error)));
        }
        term_installed_ = true;
    }

    ~SignalPipe() {
        if (int_installed_) {
            sigaction(SIGINT, &old_int_, nullptr);
        }
        if (term_installed_) {
            sigaction(SIGTERM, &old_term_, nullptr);
        }
        Close();
    }

    SignalPipe(const SignalPipe&) = delete;
    SignalPipe& operator=(const SignalPipe&) = delete;

    int ReadFd() const noexcept { return read_fd_; }

    void DisableInChild() noexcept {
        g_signal_fd = -1;
        struct sigaction action {};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGTERM, &action, nullptr);
        Close();
    }

private:
    void Close() noexcept {
        g_signal_fd = -1;
        if (read_fd_ != -1) {
            close(read_fd_);
            read_fd_ = -1;
        }
        if (write_fd_ != -1) {
            close(write_fd_);
            write_fd_ = -1;
        }
    }

    int read_fd_ = -1;
    int write_fd_ = -1;
    struct sigaction old_int_ {};
    struct sigaction old_term_ {};
    bool int_installed_ = false;
    bool term_installed_ = false;
};

void StopChild(pid_t child) {
    if (child <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child || (result == -1 && errno == ECHILD)) {
        return;
    }

    if (kill(child, SIGTERM) != 0 && errno != ESRCH) {
        std::fprintf(stderr, "не удалось отправить SIGTERM козлёнку: %s\n", std::strerror(errno));
    }
    while (waitpid(child, &status, 0) == -1 && errno == EINTR) {
    }
}

}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    pid_t child = -1;

    try {
        wolf::SharedStateArea area;
        wolf::PrepareTransport(area.Get());

        SignalPipe signalPipe;

        child = fork();
        if (child < 0) {
            throw std::runtime_error("fork: " + std::string(std::strerror(errno)));
        }
        if (child == 0) {
            signalPipe.DisableInChild();
            const int code = wolf::RunClient(area.Get());
            _exit(code);
        }

        std::unique_ptr<wolf::Conn> connection;
        try {
            connection = wolf::OpenTransport(wolf::Role::Host, area.Get());
        } catch (...) {
            StopChild(child);
            throw;
        }

        int code = 0;
        {
            QApplication application(argc, argv);
            QSocketNotifier notifier(signalPipe.ReadFd(), QSocketNotifier::Read);
            QObject::connect(&notifier, &QSocketNotifier::activated, &application,
                             [&application]() { application.quit(); });

            wolf::HostWindow window(connection.get(), child);
            window.show();
            code = application.exec();
        }

        StopChild(child);
        child = -1;
        connection.reset();
        return code;
    } catch (const std::exception& exception) {
        StopChild(child);
        std::fprintf(stderr, "ошибка: %s\n", exception.what());
        return 1;
    }
}
