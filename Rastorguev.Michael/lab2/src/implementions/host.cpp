#include <QApplication>
#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

#include "conn.h"
#include "kid.h"
#include "logger.h"
#include "mainwindow.h"

static void ReapChild(pid_t pid) {
    int status = 0;
    for (int i = 0; i < 40; ++i) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            Log("host", "Козлёнок завершился, код %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            return;
        }
        if (r < 0) return;
        usleep(50 * 1000);
    }
    Log("host", "Козлёнок не завершился сам, отправляю SIGKILL");
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
}

int main(int argc, char** argv) {
    const pid_t hostPid = getpid();
    Conn* conn = nullptr;

    if (Conn::kCreateBeforeFork) {
        conn = new Conn(hostPid, true);
        if (!conn->Ok()) {
            Log("host", "Не удалось создать соединение");
            delete conn;
            return 1;
        }
    }

    pid_t kid = fork();
    if (kid < 0) {
        perror("fork");
        delete conn;
        return 1;
    }

    if (kid == 0) {
        if (!Conn::kCreateBeforeFork) conn = new Conn(hostPid, false);
        int rc = (conn && conn->Ok()) ? RunKid(*conn) : 1;
        delete conn;
        _exit(rc);
    }

    if (!Conn::kCreateBeforeFork) {
        conn = new Conn(hostPid, true);
        if (!conn->Ok()) {
            Log("host", "Не удалось создать соединение");
            kill(kid, SIGTERM);
            waitpid(kid, nullptr, 0);
            delete conn;
            return 1;
        }
    }

    qputenv("XDG_CURRENT_DESKTOP", "X-Generic");
    qputenv("QT_QPA_PLATFORMTHEME", "generic");

    int rc;
    {
        QApplication app(argc, argv);
        MainWindow window(conn, kid);
        window.show();
        rc = app.exec();
    }
    ReapChild(kid);
    delete conn;
    return rc;
}
