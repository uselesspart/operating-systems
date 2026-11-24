#include "process_manager.h"

#include <csignal>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>

namespace {

    void require(bool condition, std::string_view message) {
        if (!condition) {
            throw std::runtime_error(std::string(message));
        }
    }

    int exchange_with_parent(void* context) {
        auto& sync = *static_cast<SharedSemaphores*>(context);

        sync.notify_host();

        return sync.wait_for_client() ? 0 : 2;
    }

    int idle_child(void*) {
        for (;;) {
            pause();
        }
    }

    void test_fork_and_semaphores() {
        SharedSemaphores sync;

        auto child = ChildProcess::spawn(
            exchange_with_parent, &sync
        );

        require(child.pid() > 0, "fork must return child PID");

        require(
            sync.wait_for_host(),
            "host must receive child notification"
        );

        sync.notify_client();

        const int status = child.wait();

        require(
            WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "child must finish successfully"
        );
    }

    void test_sigkill() {
        auto child = ChildProcess::spawn(idle_child, nullptr);

        child.terminate(SIGKILL);

        const int status = child.wait();

        require(
            WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL,
            "client must terminate with SIGKILL"
        );
    }

    void test_semaphore_timeout() {
        SharedSemaphores sync;

        require(
            !sync.wait_for_host(),
            "waiting must time out after five seconds"
        );
    }

    void test_message_write_ordering() {
        SharedSemaphores sync;

        require(
            sync.wait_for_host_write(),
            "host can send the first message"
        );

        require(
            !sync.wait_for_host_write(),
            "host cannot send again without acknowledgment"
        );

        sync.acknowledge_host_message();

        require(
            sync.wait_for_host_write(),
            "host can send after client acknowledgment"
        );

        sync.acknowledge_host_message();

        require(
            sync.wait_for_client_write(),
            "client has an independent write permission"
        );

        sync.acknowledge_client_message();
    }

} // namespace

int main() {
    try {
        test_fork_and_semaphores();
        test_sigkill();
        test_semaphore_timeout();
        test_message_write_ordering();

        std::cout << "All process manager tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: "
                  << error.what() << '\n';
        return 1;
    }
}