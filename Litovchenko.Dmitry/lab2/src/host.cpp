#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <random>
#include <string>

#include "common.h"
#include "conn.h"
#include "goat.h"
#include "wolf_session.h"
#include "wolf_window.h"

namespace {

std::atomic<bool> g_stop{false};

void onStopSignal(int) {
    g_stop = true;
}

struct Options {
    bool gui = true;
    int rounds = 0;
    unsigned seed = 0;
    int wolf = 0;
};

void usage() {
    std::fprintf(stderr,
                 "Usage: %s [--no-gui] [--rounds N] [--seed S] [--wolf N]\n"
                 "  --no-gui    play in the terminal, the wolf number is random or --wolf\n"
                 "  --rounds N  stop after N rounds (and close the window)\n"
                 "  --seed S    seed for random numbers\n",
                 program_invocation_short_name);
}

bool parseInt(const char* text, int min, int max, int& out) {
    char* end = nullptr;
    long value = std::strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value < min || value > max) return false;
    out = static_cast<int>(value);
    return true;
}

bool parseArgs(int argc, char* argv[], Options& opt) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        int value = 0;
        bool hasValue = i + 1 < argc;
        if (arg == "--no-gui") {
            opt.gui = false;
        } else if (arg == "--rounds" && hasValue && parseInt(argv[++i], 1, 1000000, value)) {
            opt.rounds = value;
        } else if (arg == "--seed" && hasValue && parseInt(argv[++i], 1, 2000000000, value)) {
            opt.seed = static_cast<unsigned>(value);
        } else if (arg == "--wolf" && hasValue && parseInt(argv[++i], 1, 100, value)) {
            opt.wolf = value;
        } else if (opt.gui && arg.rfind("-", 0) == 0 && arg.rfind("--", 0) != 0) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

int runTerminal(Shared* shared, pid_t goat, int id, const Options& opt) {
    std::mt19937 rng(opt.seed);
    WolfSession::Callbacks cb;
    cb.status = [](const std::string& text) { std::printf("status: %s\n", text.c_str()); };
    cb.wolfNumber = [&](int) {
        if (opt.wolf) return WolfChoice{opt.wolf, true};
        return WolfChoice{std::uniform_int_distribution<int>(1, 100)(rng), false};
    };
    cb.roundDone = [](const RoundResult& r) {
        std::printf("round=%d wolf=%d goat=%d before=%s after=%s hidden=%d caught=%d revived=%d dead=%d\n",
                    r.round, r.wolf, r.numbers[0], r.wasAlive[0] ? "alive" : "dead",
                    r.alive[0] ? "alive" : "dead", r.hidden, r.caught, r.revived, r.dead);
        std::fflush(stdout);
    };
    struct sigaction sa{};
    sa.sa_handler = onStopSignal;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    return WolfSession(shared, goat, id, cb).run(opt.rounds, g_stop);
}

}

int main(int argc, char* argv[]) {
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        usage();
        return 2;
    }
    if (opt.seed == 0) opt.seed = static_cast<unsigned>(std::time(nullptr) ^ getpid());
    signal(SIGPIPE, SIG_IGN);

    Shared* shared = nullptr;
    try {
        shared = createShared();
    } catch (const std::exception& e) {
        logMsg("%s", e.what());
        return 1;
    }
    const int id = getpid();
    std::fflush(nullptr);

    pid_t goat = fork();
    if (goat < 0) {
        logMsg("fork: %s", std::strerror(errno));
        return 1;
    }
    if (goat == 0) _exit(runGoat(shared, id, opt.seed + 1));

    logMsg("волк запущен, связь %s, козлёнок pid %d", Conn::Type(), goat);
    int code = opt.gui ? runGui(argc, argv, shared, goat, id, opt.rounds) : runTerminal(shared, goat, id, opt);
    destroyShared(shared);
    logMsg("волк завершился, код %d", code);
    return code;
}
