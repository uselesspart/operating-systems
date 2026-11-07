#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <QApplication>
#include <QMessageBox>
#include <QString>
#include <QTimer>

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>

#include "chat_window.h"
#include "connection.h"

namespace {

constexpr int kStopPollMs = 200;

volatile std::sig_atomic_t g_stopRequested = 0;

void requestStop(int)
{
    g_stopRequested = 1;
}

int showFatal(int& argc, char** argv, const QString& text)
{
    std::fprintf(stderr, "%s\n", text.toUtf8().constData());
    QApplication app(argc, argv);
    QMessageBox::critical(nullptr, QStringLiteral("Ошибка"), text);
    return 1;
}

void stopChild(pid_t pid)
{
    for (int waitedMs = 0; waitedMs < kSyncTimeoutMs; waitedMs += 50) {
        const pid_t result = waitpid(pid, nullptr, WNOHANG);
        if (result == pid || (result < 0 && errno != EINTR))
            return;
        usleep(50000);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
    }
}

}

int main(int argc, char* argv[])
{
    std::signal(SIGPIPE, SIG_IGN);
    for (const int signalNumber : {SIGINT, SIGTERM, SIGHUP})
        std::signal(signalNumber, requestStop);

    std::unique_ptr<Connection> connection;
    try {
        connection = createConnection();
    } catch (const std::exception& error) {
        return showFatal(argc, argv, QString::fromUtf8(error.what()));
    }

    const pid_t parent = getpid();
    const pid_t child = fork();
    if (child < 0) {
        return showFatal(argc, argv,
                         QStringLiteral("Не удалось создать клиентский процесс (fork): ")
                             + QString::fromLocal8Bit(std::strerror(errno)));
    }

    const bool isHost = child > 0;
    if (!isHost) {
        prctl(PR_SET_PDEATHSIG, SIGKILL);
        if (getppid() != parent)
            return 1;
    }

    const Party self = isHost ? Party::Host : Party::Client;
    if (!connection->connect(self)) {
        const QString text = QStringLiteral("Не удалось установить соединение: ")
                             + QString::fromStdString(connection->error());
        if (!isHost) {
            std::fprintf(stderr, "%s\n", text.toUtf8().constData());
            return 1;
        }
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        return showFatal(argc, argv, text);
    }

    QApplication app(argc, argv);
    ChatWindow window(*connection, self, isHost ? child : 0);
    window.show();

    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, &window, [&window] {
        if (g_stopRequested)
            window.close();
    });
    stopPoll.start(kStopPollMs);

    const int code = app.exec();

    if (isHost)
        stopChild(child);
    return code;
}
