#include "session.hpp"
#include "window.hpp"
#include <QApplication>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
int headless(lab::Session& session) {
    session.waitReady();
    while (!session.finished() && !lab::interrupted) {
        if (session.options().delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(session.options().delayMs));
        if (lab::interrupted) break;
        const auto round = session.play(session.nextWolf());
        std::cout << "{\"event\":\"round\",\"round\":" << round.number << ",\"wolf\":" << round.wolf
                  << ",\"goat\":" << round.goat << ",\"alive\":" << round.alive << ",\"hidden\":" << round.hidden
                  << ",\"caught\":" << round.caught << ",\"resurrected\":" << round.resurrected
                  << ",\"dead\":" << round.dead << ",\"finished\":" << round.finished << "}" << std::endl;
    }
    const char* reason = lab::interrupted ? "signal" : (session.game().finished() ? "two_dead_rounds" : "round_limit");
    session.stop();
    std::cout << "{\"event\":\"finished\",\"reason\":\"" << reason << "\"}" << std::endl;
    return lab::interrupted ? 128 + lab::interrupted : 0;
}
}
int main(int argc, char** argv) {
    try {
        const auto options = lab::parseOptions(argc, argv);
        if (options.help) { std::cout << lab::usage(); return 0; }
        lab::Session session(options);
        try {
            if (options.headless) return headless(session);
            // Qt sees only argv[0]; application options have already been parsed.
            int qtArgc = 1;
            QApplication application(qtArgc, argv);
            lab::Window window(session);
            window.show();
            application.exec();
            return window.failed() ? 1 : (lab::interrupted ? 128 + lab::interrupted : 0);
        } catch (const std::exception& error) {
            session.logger().write(std::string("host error: ") + error.what());
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
