#include "chat.hpp"
#include "connection.hpp"
#include <exception>
#include <iostream>
#include <unistd.h>

int main(int argc, char** argv) {
    try {
        const auto options = chat::options(argc, argv, true);
        if (options.help) {
            std::cout << "Usage: client_" << chat::transport_name() << " HOST_PID [--headless]\n"
                      << "Commands: text, /to PID text (0 = host), /quit\n";
            return 0;
        }
        chat::block_signals();
        chat::Events events;
        chat::Client client(options.host, events);
        return chat::run_application(argc, argv, options.headless,
            "Клиент " + std::to_string(getpid()) + " — " + chat::transport_name(), events,
            [&](int recipient, const std::string& text) { client.send(recipient, text); },
            [&] { return client.alive(); });
    } catch (const std::exception& error) { std::cerr << "ERROR " << error.what() << '\n'; return 1; }
}
