// Exercise the real window destructor, including a close before worker startup.
#define LAB2_HOST_NO_MAIN
#include "../src/host.cpp"

#include <cassert>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const int delay = argc > 1 ? std::atoi(argv[1]) : 600;
    {
        HostWindow window;
        window.show();
        QTimer::singleShot(delay, &window, &QWidget::close);
        if (app.exec() != 0) return 1;
    }
    // The destructor must join the worker and reap its child.
    int status = 0;
    assert(waitpid(-1, &status, WNOHANG) == -1 && errno == ECHILD);
#if defined(TYPE_SHM)
    const int fd = shm_open("/lab2_shm", O_RDONLY, 0);
    assert(fd == -1 && errno == ENOENT);
#endif
    return 0;
}
