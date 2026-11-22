#include <signal.h>
#include <unistd.h>

#include <QApplication>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "conn/conn.h"
#include "gui/HostWindow.h"
#include "host/HostServer.h"
#include "ipc/Signals.h"
#include "ipc/Timeouts.h"
#include "util/Log.h"

namespace {

bool parseInt(const char* s, long& out) {
    char* end = nullptr;
    out = std::strtol(s, &end, 10);
    return end != s && *end == '\0';
}

int usage(const char* argv0) {
    std::fprintf(stderr,
                 "Использование: %s [--kids N] [--turn-ms N] [--wolf N] [--seed N]\n"
                 "  --kids N     сколько козлят породить при запуске (0..%d, по умолчанию 3)\n"
                 "  --turn-ms N  сколько ждать ввод числа волка, мс (100..4000, по умолчанию 3000)\n"
                 "  --wolf N     число волка вместо случайного, если пользователь не ввёл своё\n"
                 "  --seed N     воспроизводимые случайные числа\n",
                 argv0, wolf::kMaxKids);
    return 2;
}

}

int main(int argc, char* argv[]) {
    wolf::hostsignals::blockHostSignals();
    ::signal(SIGPIPE, SIG_IGN);

    const std::string type = wolf::Conn::typeCode();
    wolf::log::init("host_" + type);

    wolf::HostOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        long value = 0;
        if (arg == "-h" || arg == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (i + 1 >= argc || !parseInt(argv[i + 1], value)) return usage(argv[0]);
        ++i;
        if (arg == "--kids" && value >= 0 && value <= wolf::kMaxKids) {
            options.kids = static_cast<int>(value);
        } else if (arg == "--turn-ms" && value >= 100 && value <= 4000) {
            options.turn = std::chrono::milliseconds(value);
        } else if (arg == "--wolf" && value >= 1 && value <= 100) {
            options.fixedWolf = static_cast<int>(value);
        } else if (arg == "--seed" && value >= 0) {
            options.seed = static_cast<unsigned>(value);
        } else {
            return usage(argv[0]);
        }
    }

    QApplication app(argc, argv);
    wolf::HostWindow window(QString::fromStdString(type), ::getpid());
    wolf::HostServer server(options, window);
    window.onWolfNumber = [&server](int n) { server.submitWolfNumber(n); };
    window.onAddKid = [&server] { server.addKid(); };

    window.show();
    server.start();
    const int rc = QApplication::exec();
    server.stop();
    return rc;
}
