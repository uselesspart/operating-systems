#include "client.hpp"
#include "connection/conn.hpp"
#include "gui/host_window.hpp"
#include "shared_sync.hpp"

#include <QApplication>
#include <QObject>
#include <QSocketNotifier>

#include <cerrno>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef WOLF_TRANSPORT
#error "WOLF_TRANSPORT must be defined by the build system"
#endif

namespace {

volatile sig_atomic_t g_signal_pipe_write_fd = -1;

void HandleTerminationSignal(int signal_number) {
    const int saved_errno = errno;
    const int fd = static_cast<int>(g_signal_pipe_write_fd);
    if (fd >= 0) {
        const unsigned char signal_byte = static_cast<unsigned char>(signal_number);
        const ssize_t ignored = write(fd, &signal_byte, sizeof(signal_byte));
        (void)ignored;
    }
    errno = saved_errno;
}

class SignalPipe {
public:
    SignalPipe() : read_fd_(-1), write_fd_(-1), int_installed_(false), term_installed_(false) {
        int descriptors[2] = {-1, -1};
        if (pipe2(descriptors, O_NONBLOCK | O_CLOEXEC) == -1) {
            throw std::system_error(errno, std::generic_category(), "pipe2 failed");
        }
        read_fd_ = descriptors[0];
        write_fd_ = descriptors[1];

        struct sigaction action{};
        action.sa_handler = HandleTerminationSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = 0;
        g_signal_pipe_write_fd = write_fd_;

        if (sigaction(SIGINT, &action, &old_int_) == -1) {
            const int error = errno;
            Close();
            throw std::system_error(error, std::generic_category(), "sigaction(SIGINT) failed");
        }
        int_installed_ = true;

        if (sigaction(SIGTERM, &action, &old_term_) == -1) {
            const int error = errno;
            sigaction(SIGINT, &old_int_, nullptr);
            int_installed_ = false;
            Close();
            throw std::system_error(error, std::generic_category(), "sigaction(SIGTERM) failed");
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

    int ReadFd() const noexcept {
        return read_fd_;
    }

    void DisableInChild() noexcept {
        g_signal_pipe_write_fd = -1;
        struct sigaction default_action{};
        default_action.sa_handler = SIG_DFL;
        sigemptyset(&default_action.sa_mask);
        sigaction(SIGINT, &default_action, nullptr);
        sigaction(SIGTERM, &default_action, nullptr);
        Close();
    }

private:
    void Close() noexcept {
        g_signal_pipe_write_fd = -1;
        if (read_fd_ != -1) {
            close(read_fd_);
            read_fd_ = -1;
        }
        if (write_fd_ != -1) {
            close(write_fd_);
            write_fd_ = -1;
        }
    }

    int read_fd_;
    int write_fd_;
    struct sigaction old_int_{};
    struct sigaction old_term_{};
    bool int_installed_;
    bool term_installed_;
};

void StopChild(pid_t child_pid) {
    if (child_pid <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = waitpid(child_pid, &status, WNOHANG);
    if (result == child_pid || (result == -1 && errno == ECHILD)) {
        return;
    }

    if (kill(child_pid, SIGTERM) == -1 && errno != ESRCH) {
        std::cerr << "Warning: cannot send SIGTERM to child: " << errno << '\n';
    }
    while (waitpid(child_pid, &status, 0) == -1 && errno == EINTR) {
    }
}

}

int main(int argc, char* argv[]) {
    pid_t child_pid = -1;
    int host_socket = -1;
    int client_socket = -1;

    try {
        QApplication application(argc, argv);
        SignalPipe signal_pipe;
        QSocketNotifier signal_notifier(signal_pipe.ReadFd(), QSocketNotifier::Read);
        QObject::connect(&signal_notifier, &QSocketNotifier::activated, &application, [&]() {
            unsigned char signal_buffer[32];
            while (read(signal_pipe.ReadFd(), signal_buffer, sizeof(signal_buffer)) > 0) {
            }
            application.quit();
        });

        const std::string transport = WOLF_TRANSPORT;
        SharedSyncMapping shared_sync;

        if (transport == "sock") {
            int socket_pair[2] = {-1, -1};
            if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socket_pair) == -1) {
                throw std::system_error(errno, std::generic_category(), "socketpair failed");
            }
            host_socket = socket_pair[0];
            client_socket = socket_pair[1];
        }

        child_pid = fork();
        if (child_pid == -1) {
            throw std::system_error(errno, std::generic_category(), "fork failed");
        }

        if (child_pid == 0) {
            signal_pipe.DisableInChild();
            if (host_socket != -1) {
                close(host_socket);
            }
            const int result = RunClient(transport, getppid(), shared_sync.get(), client_socket);
            _exit(result);
        }

        if (client_socket != -1) {
            close(client_socket);
            client_socket = -1;
        }

        std::unique_ptr<Conn> connection = CreateConn(
            ConnRole::Host, getpid(), shared_sync.get(), host_socket);
        host_socket = -1;

        int result = 0;
        {
            HostWindow window(connection.get(), child_pid);
            window.show();
            result = application.exec();
        }

        StopChild(child_pid);
        child_pid = -1;
        connection.reset();
        return result;
    } catch (const std::exception& exception) {
        if (host_socket != -1) {
            close(host_socket);
        }
        if (client_socket != -1) {
            close(client_socket);
        }
        StopChild(child_pid);
        std::cerr << "Host error: " << exception.what() << '\n';
        return 1;
    }
}
