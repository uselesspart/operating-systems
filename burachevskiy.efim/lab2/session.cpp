#include "session.hpp"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>

void post(sem_t& semaphore) {
    if (sem_post(&semaphore) < 0) fail("sem_post");
}

void wait(sem_t& semaphore) {
    timespec deadline{};
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) fail("clock_gettime");
    deadline.tv_sec += 5;
    while (sem_clockwait(&semaphore, CLOCK_MONOTONIC, &deadline) < 0) {
        if (errno != EINTR) fail("sem_clockwait");
    }
}

Session::Session() {
    char path[] = "/tmp/lab2-chat-XXXXXX";
    if (!mkdtemp(path)) fail("mkdtemp");
    directory_ = path;
    int sockets[] = {-1, -1};
    try {
        void* memory = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE,
                            MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (memory == MAP_FAILED) fail("mmap");
        shared_ = ::new (memory) Shared{};
        for (auto* semaphore : {&shared_->started, &shared_->request, &shared_->reply}) {
            if (sem_init(semaphore, 1, 0) < 0) fail("sem_init");
            ++initialized_;
        }
#ifdef CHAT_SOCKET
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) < 0)
            fail("socketpair");
#endif
        client_ = fork();
        if (client_ < 0) fail("fork");
        if (client_ == 0) {
            std::signal(SIGTERM, SIG_DFL);
            std::signal(SIGINT, SIG_DFL);
            const int status = runClient(*shared_, directory_, sockets[0], sockets[1]);
            _exit(status);
        }
        connection_ = std::make_unique<Conn>(true, directory_, sockets[0], sockets[1]);
        sockets[0] = sockets[1] = -1;
        post(shared_->started);
    } catch (...) {
        for (int fd : sockets) if (fd >= 0) close(fd);
        cleanup();
        throw;
    }
}

Session::~Session() { cleanup(); }

void Session::stop(int signal) noexcept {
    if (client_ <= 0) return;
    kill(client_, signal);
    const auto deadline = Clock::now() + 500ms;
    int status = 0;
    for (;;) {
        const auto result = waitpid(client_, &status, WNOHANG);
        if (result == client_ || (result < 0 && errno == ECHILD)) break;
        if (Clock::now() >= deadline) {
            kill(client_, SIGKILL);
            while (waitpid(client_, &status, 0) < 0 && errno == EINTR) {}
            break;
        }
        std::this_thread::sleep_for(10ms);
    }
    client_ = -1;
}

void Session::cleanup() noexcept {
    stop(SIGTERM);
    connection_.reset();
    if (shared_) {
        sem_t* semaphores[] = {&shared_->started, &shared_->request, &shared_->reply};
        for (int i = 0; i < initialized_; ++i) sem_destroy(semaphores[i]);
        munmap(shared_, sizeof(Shared));
    }
    if (!directory_.empty()) rmdir(directory_.c_str());
}

void Session::send(const Frame& value) {
    if (waiting_) throw std::logic_error("Предыдущий запрос ещё не завершён");
    connection_->Write(value.data(), value.size());
    deadline_ = Clock::now() + 5s;
    post(shared_->request);
    waiting_ = true;
}

bool Session::receive(Frame& value) {
    if (!waiting_) return false;
    if (sem_trywait(&shared_->reply) < 0) {
        if (errno != EAGAIN && errno != EINTR) fail("sem_trywait");
        int status = 0;
        const auto result = waitpid(client_, &status, WNOHANG);
        if (result == client_) {
            client_ = -1;
            throw std::runtime_error("Клиент завершился");
        }
        if (result < 0 && errno != EINTR) fail("waitpid");
        if (Clock::now() >= deadline_) throw std::runtime_error("Тайм-аут ответа клиента (5 с)");
        return false;
    }
    connection_->Read(value.data(), value.size());
    waiting_ = false;
    return true;
}
