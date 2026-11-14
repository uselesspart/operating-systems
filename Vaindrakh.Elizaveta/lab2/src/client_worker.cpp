#include "session.hpp"
#include <random>

namespace lab {
int runClient(Connection& connection, const Options& options, const Logger& logger) {
    try {
        connection.afterFork(Role::Client);
        logger.write("client connected");
        connection.Write({Kind::Ready, 0, 0, 1, 0});
        bool alive = true;
        int round = 0;
        std::mt19937 random(options.seed ^ 0x9e3779b9U);
        while (true) {
            const Message request = connection.Read();
            if (request.kind == Kind::Stop) {
                connection.Write({Kind::Bye, round, 0, alive ? 1 : 0, 1});
                break;
            }
            expect(request, Kind::Roll, round + 1);
            if (request.alive != (alive ? 1 : 0)) throw std::runtime_error("Protocol error: inconsistent goat state");
            const int maximum = alive ? 100 : 50;
            const int number = static_cast<std::size_t>(round) < options.goats.size()
                ? options.goats[static_cast<std::size_t>(round)] : std::uniform_int_distribution<int>(1, maximum)(random);
            if (number < 1 || number > maximum) throw std::runtime_error("Goat roll outside the allowed range");
            ++round;
            connection.Write({Kind::Number, round, number, alive ? 1 : 0, 0});
            const Message status = connection.Read();
            expect(status, Kind::Status, round);
            if ((status.alive != 0 && status.alive != 1) || (status.finished != 0 && status.finished != 1))
                throw std::runtime_error("Protocol error: invalid status");
            alive = status.alive != 0;
            logger.write("client round=" + std::to_string(round) + " goat=" + std::to_string(number)
                         + " alive=" + std::to_string(alive));
            // Acknowledgement prevents overwriting the shared slot before it is read.
            connection.Write({status.finished ? Kind::Bye : Kind::Ready, round, 0, alive ? 1 : 0, status.finished});
            if (status.finished) break;
        }
        logger.write("client finished");
        return 0;
    } catch (const std::exception& error) {
        logger.write(std::string("client error: ") + error.what());
        return 1;
    }
}
}
