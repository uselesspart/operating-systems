#include "Conn.h"
#include "SharedSems.h"
#include "Protocol.h"
#include "ClientLoop.h"
#include "HostWindow.h"
#include <QApplication>

// CONN_HEADER и CONN_CLASS подставляются из CMakeLists.txt - один host.cpp обслуживает все типы IPC (host_mq, host_fifo, host_sock)
#include CONN_HEADER
using ConnImpl = CONN_CLASS;

#include <unistd.h>
#include <sys/wait.h>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <memory>
#include <stdexcept>
#include <syslog.h>

int main(int argc, char* argv[]) {
    try {
        SharedSems* sems = createSharedSems();

        // preFork создаёт общие ресурсы (очереди, FIFO, socketpair) и записывает их имена/дескрипторы в sems->. Должен быть вызван до fork, чтобы клиент унаследовал всё нужное
        ConnImpl::preFork(sems);

        pid_t pid = fork();
        if (pid < 0) {
            std::fprintf(stderr, "fork: %s\n", std::strerror(errno));
            return 1;
        }

        if (pid == 0) {
            // Ребёнок - клиент (козлёнок). Работает без Qt, открывает канал по имени из sems
            openlog("lab2_client", LOG_PID, LOG_USER);
            try {
                auto conn = ConnImpl::createClient(sems);
                runClientLoop(conn.get(), sems);
            } catch (const std::exception& e) {
                syslog(LOG_ERR, "client exception: %s", e.what());
                closelog();
                _exit(1);
            }
            closelog();
            _exit(0);
        }

        // Родитель - хост (волк). Создаёт свой конец канала и запускает Qt
        auto conn = ConnImpl::createHost(sems);

        QApplication app(argc, argv);
        HostWindow window(conn.get(), sems);
        window.show();

        int rc = app.exec();

        // Завершаем клиента и ждём, чтобы не оставить зомби
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
        return rc;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "fatal: %s\n", e.what());
        return 1;
    }
}