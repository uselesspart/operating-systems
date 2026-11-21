#include "client.hpp"

#include "connection/conn.hpp"
#include "protocol.hpp"

#include <exception>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>

int RunClient(const std::string& transport, pid_t host_pid, SharedSync* sync, int inherited_socket) {
    try {
        std::unique_ptr<Conn> connection;
        if (transport == "mq" || transport == "fifo" || transport == "sock") {
            connection = CreateConn(ConnRole::Client, host_pid, sync, inherited_socket);
        } else {
            throw std::runtime_error("Unknown transport: " + transport);
        }

        std::mt19937 generator(std::random_device{}());
        bool alive = true;
        std::cout << "[client] Connected to host PID " << host_pid << " via " << transport << std::endl;

        while (true) {
            Message request{};
            if (!connection->Read(&request, sizeof(request))) {
                throw std::runtime_error(connection->LastError());
            }
            if (request.type != kMessageRound || request.round <= 0 || request.flag != 0) {
                throw std::runtime_error("Invalid round request from host");
            }

            const int maximum = alive ? 100 : 50;
            if (request.value != maximum) {
                throw std::runtime_error("Host and client disagree about the goat state");
            }

            std::uniform_int_distribution<int> distribution(1, maximum);
            const int number = distribution(generator);
            const Message response{kMessageGoat, request.round, number, 0};
            if (!connection->Write(&response, sizeof(response))) {
                throw std::runtime_error(connection->LastError());
            }

            Message result{};
            if (!connection->Read(&result, sizeof(result))) {
                throw std::runtime_error(connection->LastError());
            }
            if (result.type != kMessageResult || result.round != request.round ||
                (result.value != 0 && result.value != 1) || (result.flag != 0 && result.flag != 1)) {
                throw std::runtime_error("Invalid game result from host");
            }

            alive = result.value == 1;
            std::cout << "[client] Round " << result.round << ": "
                      << (alive ? "alive" : "dead") << std::endl;
            if (result.flag == 1) {
                std::cout << "[client] Game over" << std::endl;
                return 0;
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << "[client] " << exception.what() << '\n';
        return 1;
    }
}
