#include "wolf/client.hpp"

#include "wolf/conn.hpp"
#include "wolf/protocol.hpp"
#include "wolf/transport.hpp"

#include <csignal>
#include <cstdlib>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <syslog.h>
#include <unistd.h>

namespace wolf {
namespace {

bool ValidRequest(const Message& message, int expected_max) {
    return message.type == RoundStart && message.round > 0 && message.value == expected_max &&
           message.flag == 0;
}

bool ValidResult(const Message& message, int round) {
    return message.type == RoundResult && message.round == round &&
           (message.value == 0 || message.value == 1) && (message.flag == 0 || message.flag == 1);
}

}

int RunClient(SharedState* state) {
    std::signal(SIGPIPE, SIG_IGN);
    openlog("wolf_client", LOG_PID | LOG_NDELAY, LOG_USER);

    try {
        std::unique_ptr<Conn> connection = OpenTransport(Role::Client, state);
        syslog(LOG_INFO, "клиент подключился, pid %d", static_cast<int>(getpid()));

        std::mt19937 generator(std::random_device{}());
        int alive = 1;

        for (;;) {
            Message request{};
            if (!connection->Read(&request, sizeof(request))) {
                syslog(LOG_ERR, "чтение от хоста: %s", connection->Error().c_str());
                closelog();
                return 1;
            }
            if (!ValidRequest(request, alive ? 100 : 50)) {
                syslog(LOG_ERR, "некорректный запрос хоста: тип %d, раунд %d, значение %d",
                       request.type, request.round, request.value);
                closelog();
                return 1;
            }

            const int max_throw = request.value;
            std::uniform_int_distribution<int> distribution(1, max_throw);
            const Message reply{GoatThrow, request.round, distribution(generator), 0};
            if (!connection->Write(&reply, sizeof(reply))) {
                syslog(LOG_ERR, "запись хосту: %s", connection->Error().c_str());
                closelog();
                return 1;
            }

            Message result{};
            if (!connection->Read(&result, sizeof(result))) {
                syslog(LOG_ERR, "чтение результата: %s", connection->Error().c_str());
                closelog();
                return 1;
            }
            if (!ValidResult(result, request.round)) {
                syslog(LOG_ERR, "некорректный результат хоста: тип %d, раунд %d",
                       result.type, result.round);
                closelog();
                return 1;
            }

            alive = result.value;
            syslog(LOG_INFO, "раунд %d: число %d, статус %s", request.round, reply.value,
                   alive ? "жив" : "мёртв");

            if (result.flag == 1) {
                syslog(LOG_INFO, "игра завершена, клиент останавливается");
                closelog();
                return 0;
            }
        }
    } catch (const std::exception& exception) {
        syslog(LOG_ERR, "клиент: %s", exception.what());
        closelog();
        return 1;
    }
}

}
