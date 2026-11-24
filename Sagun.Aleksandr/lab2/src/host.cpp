#include "runtime/sessions.hpp"

#include <cerrno>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

int clientCount(int argc, char** argv)
{
    if (argc == 1) return 1;
    int count = 0;
    if (argc == 3 && std::string_view(argv[1]) == "--clients") {
        std::string_view value(argv[2]);
        auto end = value.data() + value.size();
        auto parsed = std::from_chars(value.data(), end, count);
        if (parsed.ec == std::errc{} && parsed.ptr == end && count > 0) return count;
    }
    throw std::invalid_argument("Запуск: host_<транспорт> [--clients N], N >= 1");
}

void finish(Sessions& sessions)
{
    for (auto& s : sessions) s->channel->close();

    // Все дети одновременно получают EOF. Если ребёнок остановлен или
    // перестал отвечать, ограничиваем время штатного завершения всей группы.
    using namespace std::chrono;
    const auto deadline = steady_clock::now() + seconds(5);
    while (steady_clock::now() < deadline) {
        bool anyAlive = false;
        for (auto& s : sessions) {
            if (stillRunning(*s)) anyAlive = true;
        }
        if (!anyAlive) return;
        std::this_thread::sleep_for(milliseconds(25));
    }
    for (auto& s : sessions) {
        if (s->pid <= 0) continue;
        if (kill(s->pid, SIGKILL) == -1 && errno != ESRCH) std::perror("kill");
        pid_t status;
        do {
            status = waitpid(s->pid, nullptr, 0);
        } while (status == -1 && errno == EINTR);
        if (status == -1 && errno != ECHILD) std::perror("waitpid");
        s->pid = -1;
    }
}

} // namespace

bool stillRunning(Session& s)
{
    if (s.pid <= 0) return false;
    pid_t result;
    do {
        result = waitpid(s.pid, nullptr, WNOHANG);
    } while (result == -1 && errno == EINTR);
    if (result == 0) return true;
    if (result == -1 && errno != ECHILD) std::perror("waitpid");
    s.pid = -1;
    return false;
}

int main(int argc, char** argv)
{
    if (std::signal(SIGCHLD, SIG_DFL) == SIG_ERR ||
        std::signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        std::perror("signal");
        return 1;
    }

    Sessions sessions;
    int code = 1;
    try {
        const int count = clientCount(argc, argv);
        for (int i = 0; i < count; ++i) {
            sessions.push_back(std::make_unique<Session>(i + 1));
            auto& current = *sessions.back();
            current.pid = fork();
            if (current.pid == -1) {
                throw std::system_error(errno, std::generic_category(), "fork");
            }
            if (current.pid == 0) {
                try {
                    auto mine = std::move(current.channel);
                    // Закрыть в ребёнке копии ВСЕХ чужих соединений.
                    sessions.clear();
                    mine->selectSide(false);
                    const int childCode = runClient(argc, argv, i + 1, count, *mine);
                    _exit(childCode);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "client: %s\n", e.what());
                    _exit(1);
                }
            }
            // Для FIFO завершаем открытие этой пары, прежде чем создавать следующую.
            current.channel->selectSide(true);
            current.idle.start();
        }
        // Все fork() у хоста уже завершены. Теперь можно запустить Qt и потоки.
        code = runHost(argc, argv, sessions);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "host: %s\n", e.what());
    }
    finish(sessions);
    return code;
}
