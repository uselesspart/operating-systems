#include "session.hpp"

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <thread>

namespace {
Frame exchange(Session& session, const Frame& request) {
    session.send(request);
    Frame reply{};
    while (!session.receive(reply)) std::this_thread::sleep_for(5ms);
    return reply;
}

void require(bool condition, const char* error) {
    if (!condition) throw std::runtime_error(error);
}
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        int input[2];
        if (pipe2(input, O_CLOEXEC) < 0) fail("pipe2");
        if (dup2(input[0], STDIN_FILENO) < 0) fail("dup2");
        close(input[0]);
        const std::string commands = "Привет\n/host секрет\n/quit\n";
        require(write(input[1], commands.data(), commands.size()) == static_cast<ssize_t>(commands.size()), "write stdin");
        close(input[1]);
        {
            Session session;
            require(exchange(session, frame('N'))[0] == 'C', "Клиент не подключился");
            const auto general = exchange(session, frame('D', "[Общее] Хост: ответ"));
            require(general[0] == 'A' && message(general) == "Привет", "Ошибка общего сообщения");
            const auto personal = exchange(session, frame('N'));
            require(personal[0] == 'P' && message(personal) == "секрет", "Ошибка личного сообщения");
            require(exchange(session, frame('N'))[0] == 'Q', "Ошибка завершения клиента");
        }
        {
            Session session;
            require(exchange(session, frame('N'))[0] == 'C', "Повторный запуск не удался");
            std::this_thread::sleep_for(5400ms);
            bool failed = false;
            try {
                exchange(session, frame('N'));
            } catch (const std::exception&) {
                failed = true;
            }
            require(failed, "Клиент должен закончить работу через 5 секунд без запросов");
        }
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
