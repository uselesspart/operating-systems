#include "session.hpp"
#include "ipc_support.hpp"
#include <cerrno>
#include <chrono>
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace lab {
volatile std::sig_atomic_t interrupted = 0;
namespace {
void onSignal(int value) { interrupted = value; }
void setSignal(int number, void (*handler)(int)) {
    struct sigaction action{};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    if (sigaction(number, &action, nullptr) == -1) systemError("sigaction");
}
}
Session::Session(const Options& options)
    : options_(options), logger_(options.logPath), connection_(createConnection()), random_(options.seed) {
    setSignal(SIGPIPE, SIG_IGN);
    setSignal(SIGINT, onSignal);
    setSignal(SIGTERM, onSignal);
    child_ = fork(); // QApplication and worker threads are created only AFTER fork.
    if (child_ == -1) systemError("fork");
    if (child_ == 0) {
        int code = 1;
        try {
            setSignal(SIGINT, SIG_DFL);
            setSignal(SIGTERM, SIG_DFL);
            code = runClient(*connection_, options_, logger_);
        } catch (const std::exception& error) { logger_.write(std::string("client setup error: ") + error.what()); }
        connection_.reset();
        _exit(code);
    }
    try {
        connection_->afterFork(Role::Host);
        logger_.write("host started transport=" TYPE_CODE " child=" + std::to_string(child_) + " seed=" + std::to_string(options.seed));
        std::cout << "{\"event\":\"started\",\"host_pid\":" << getpid()
                  << ",\"client_pid\":" << child_ << ",\"transport\":\"" TYPE_CODE "\"}" << std::endl;
    } catch (...) {
        stop();
        throw;
    }
}
Session::~Session() { stop(); }
void Session::waitReady() {
    expect(connection_->Read(), Kind::Ready, 0);
    connected_ = true;
    logger_.write("host: client joined");
}
int Session::nextWolf() {
    const auto index = static_cast<std::size_t>(game_.round());
    return index < options_.wolves.size() ? options_.wolves[index] : std::uniform_int_distribution<int>(1, 100)(random_);
}
bool Session::limited() const { return options_.roundLimit > 0 && game_.round() >= options_.roundLimit; }
Round Session::play(int wolf) {
    if (!connected_ || finished()) throw std::logic_error("Session is not ready for a turn");
    if (wolf < 1 || wolf > 100) throw std::invalid_argument("Wolf roll must be in 1..100");
    const int round = game_.round() + 1;
    connection_->Write({Kind::Roll, round, 0, game_.alive() ? 1 : 0, 0});
    const Message answer = connection_->Read();
    expect(answer, Kind::Number, round);
    if (answer.alive != (game_.alive() ? 1 : 0)) throw std::runtime_error("Protocol error: goat state differs");
    const Round result = game_.play(wolf, answer.value);
    connection_->Write({Kind::Status, round, 0, result.alive ? 1 : 0, finished() ? 1 : 0});
    expect(connection_->Read(), finished() ? Kind::Bye : Kind::Ready, round);
    logger_.write("host round=" + std::to_string(round) + " wolf=" + std::to_string(wolf)
                  + " goat=" + std::to_string(answer.value) + " alive=" + std::to_string(result.alive));
    return result;
}
void Session::stop() noexcept {
    if (closing_ || child_ <= 0) return;
    closing_ = true;
    try {
        if (connected_ && !finished()) connection_->Write({Kind::Stop, game_.round(), 0, 0, 1});
    } catch (const std::exception& error) { logger_.write(std::string("stop: ") + error.what()); }
    int status = 0;
    bool reaped = false;
    // Bound graceful shutdown, then terminate/reap the child before IPC destruction.
    for (int i = 0; i < 200; ++i) {
        const pid_t result = waitpid(child_, &status, WNOHANG);
        if (result == child_ || (result == -1 && errno == ECHILD)) { reaped = true; break; }
        if (result == -1 && errno != EINTR) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!reaped) {
        kill(child_, SIGKILL);
        while (waitpid(child_, &status, 0) == -1 && errno == EINTR) {}
        logger_.write("client forcibly stopped");
    }
    logger_.write("host stopped; child reaped, status=" + std::to_string(status));
    child_ = -1;
}
}
