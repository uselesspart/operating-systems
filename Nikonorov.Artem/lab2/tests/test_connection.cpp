#include "../src/connection/conn.hpp"
#include "../src/protocol.hpp"
#include "../src/shared_sync.hpp"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

[[noreturn]] void ChildRun(pid_t host_pid, SharedSync* sync, int inherited_socket) {
    try {
        std::unique_ptr<Conn> connection = CreateConn(ConnRole::Client, host_pid, sync, inherited_socket);
        Message request{};
        Require(connection->Read(&request, sizeof(request)), "client request read failed: " + connection->LastError());
        Require(request.type == kMessageRound && request.round == 1,
                "client received an invalid round request");

        const Message response{kMessageGoat, 1, 42, 0};
        Require(connection->Write(&response, sizeof(response)), "client response write failed: " + connection->LastError());

        Message result{};
        Require(connection->Read(&result, sizeof(result)), "client result read failed: " + connection->LastError());
        Require(result.type == kMessageResult && result.round == 1 && result.value == 1,
                "client received an invalid result");
        _exit(EXIT_SUCCESS);
    } catch (const std::exception& exception) {
        std::cerr << "Connection-test child failed: " << exception.what() << '\n';
        _exit(EXIT_FAILURE);
    }
}

void RunRoundTripTest() {
    SharedSyncMapping shared_sync;
    const pid_t host_pid = getpid();
    int host_socket = -1;
    int client_socket = -1;
#ifdef WOLF_TEST_SOCKETPAIR
    int socket_pair[2] = {-1, -1};
    Require(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socket_pair) == 0,
            "socketpair failed: " + std::to_string(errno));
    host_socket = socket_pair[0];
    client_socket = socket_pair[1];
#endif

    const pid_t child_pid = fork();
    if (child_pid == -1) {
        if (host_socket != -1) {
            close(host_socket);
        }
        if (client_socket != -1) {
            close(client_socket);
        }
        throw std::runtime_error("fork failed: " + std::to_string(errno));
    }

    if (child_pid == 0) {
        if (host_socket != -1) {
            close(host_socket);
        }
        ChildRun(host_pid, shared_sync.get(), client_socket);
    }
    if (client_socket != -1) {
        close(client_socket);
        client_socket = -1;
    }

    int status = 0;
    try {
        std::unique_ptr<Conn> connection = CreateConn(ConnRole::Host, host_pid, shared_sync.get(), host_socket);
        host_socket = -1;
        const Message request{kMessageRound, 1, 100, 0};
        Require(connection->Write(&request, sizeof(request)),
                "host request write failed: " + connection->LastError());

        Message response{};
        Require(connection->Read(&response, sizeof(response)),
                "host response read failed: " + connection->LastError());
        Require(response.type == kMessageGoat && response.round == 1 && response.value == 42,
                "host received an invalid client response");

        const Message result{kMessageResult, 1, 1, 0};
        Require(connection->Write(&result, sizeof(result)),
                "host result write failed: " + connection->LastError());

        const pid_t waited = waitpid(child_pid, &status, 0);
        Require(waited == child_pid, "waitpid failed");
        Require(WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS,
                "client process reported a failure");
    } catch (...) {
        if (host_socket != -1) {
            close(host_socket);
        }
        kill(child_pid, SIGTERM);
        while (waitpid(child_pid, &status, 0) == -1 && errno == EINTR) {
        }
        throw;
    }
}

void RunCancellationTest() {
    SharedSyncMapping shared_sync;
    const pid_t host_pid = getpid();
    int host_socket = -1;
    int client_socket = -1;
#ifdef WOLF_TEST_SOCKETPAIR
    int socket_pair[2] = {-1, -1};
    Require(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, socket_pair) == 0,
            "socketpair failed in cancellation test: " + std::to_string(errno));
    host_socket = socket_pair[0];
    client_socket = socket_pair[1];
#endif

    std::unique_ptr<Conn> connection = CreateConn(
        ConnRole::Host, host_pid, shared_sync.get(), host_socket);
    host_socket = -1;

    std::atomic<bool> read_result{true};
    std::thread reader([&]() {
        Message message{};
        read_result.store(connection->Read(&message, sizeof(message)));
    });

    usleep(100000);
    connection->Cancel();
    reader.join();

    Require(!read_result.load(), "cancelled read unexpectedly succeeded");
    connection.reset();
    if (client_socket != -1) {
        close(client_socket);
    }
}

}

int main() {
    try {
        RunRoundTripTest();
        RunCancellationTest();
        std::cout << "IPC round-trip and cancellation tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "IPC integration test failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
