#include <QApplication>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <semaphore.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>

#include "common.h"

#if defined(TYPE_MMAP)
#include "conn_mmap.h"
using Channel = ConnMmap;
#define CHANNEL_AFTER_FORK 0
#elif defined(TYPE_SHM)
#include "conn_shm.h"
using Channel = ConnShm;
#define CHANNEL_AFTER_FORK 1
#elif defined(TYPE_PIPE)
#include "conn_pipe.h"
using Channel = ConnPipe;
#define CHANNEL_AFTER_FORK 0
#else
#error "Define exactly one of TYPE_MMAP, TYPE_SHM, TYPE_PIPE"
#endif

static constexpr size_t MSG = 32;

struct Sync {
    sem_t toKid;
    sem_t toHost;
};

static bool semWait(sem_t* s, int ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += ms / 1000;
    ts.tv_nsec += static_cast<long>(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    while (sem_timedwait(s, &ts) == -1) {
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

static void kidLoop(Conn* channel, Sync* sync) {
    logMsg("Kid started");
    bool alive = true;
    for (;;) {
        if (!semWait(&sync->toKid, 5000)) {
            logMsg("Kid: timeout waiting for host, stopping");
            break;
        }
        char wbuf[MSG] = {};
        if (!channel->Read(wbuf, sizeof(wbuf))) {
            logMsg("Kid: channel read failed, stopping");
            break;
        }
        if (std::strncmp(wbuf, "END", 3) == 0) break;

        int maxValue = alive ? 100 : 50;
        int kid = (std::rand() % maxValue) + 1;
        char kb[MSG] = {};
        std::snprintf(kb, sizeof(kb), "%d", kid);
        channel->Write(kb, sizeof(kb));
        sem_post(&sync->toHost);

        if (!semWait(&sync->toKid, 5000)) {
            logMsg("Kid: timeout waiting for result, stopping");
            break;
        }
        char res[MSG] = {};
        if (!channel->Read(res, sizeof(res))) {
            logMsg("Kid: channel read failed, stopping");
            break;
        }
        alive = (std::strncmp(res, "DEAD", 4) != 0);
        sem_post(&sync->toHost); // ack: result consumed
        logMsg(std::string("Kid sent ") + kb + ", got " + res);
    }
    logMsg("Kid finished");
}

class Worker : public QThread {
    Q_OBJECT
    Conn* channel;
    Sync* sync;
    QSemaphore* inputReady;
    std::atomic<int>* inputValue;
    int rounds = 0, deadStreak = 0, wolfNum = 0, kidNum = 0;
    bool kidAlive = true;
    bool gameOver = false;

public:
    Worker(Conn* ch, Sync* sy, QSemaphore* input, std::atomic<int>* value,
           QObject* parent = nullptr)
        : QThread(parent), channel(ch), sync(sy), inputReady(input), inputValue(value) {}

signals:
    void msg(const QString& text);

protected:
    void run() override {
        emit msg("Kid connected. Enter a wolf number (or wait 3 s for a random one).");
        for (;;) {
            if (gameOver) break;
            rounds++;

            int wolf = (std::rand() % 100) + 1;
            if (inputReady->tryAcquire(1, 3000)) {
                int typed = inputValue->load();
                if (typed >= 1 && typed <= 100) wolf = typed;
            }
            wolfNum = wolf;

            char wstr[MSG] = {};
            std::snprintf(wstr, sizeof(wstr), "%d", wolfNum);
            channel->Write(wstr, sizeof(wstr));
            emit msg(QString("Round %1 | Wolf: %2").arg(rounds).arg(wolfNum));
            sem_post(&sync->toKid);

            if (!semWait(&sync->toHost, 5000)) {
                emit msg("Timeout - kid is not responding");
                gameOver = true;
                break;
            }
            char kb[MSG] = {};
            if (!channel->Read(kb, sizeof(kb))) {
                emit msg("Connection error - kid is not responding");
                gameOver = true;
                break;
            }
            kidNum = std::atoi(kb);
            logMsg("Host r" + std::to_string(rounds) + " wolf=" + std::to_string(wolfNum) +
                   " kid=" + std::to_string(kidNum));

            bool wasAlive = kidAlive;
            kidAlive = kidNextAlive(wasAlive, wolfNum, kidNum);
            if (kidAlive) deadStreak = 0; else deadStreak++;

            char res[MSG] = {};
            std::snprintf(res, sizeof(res), "%s", kidAlive ? "ALIVE" : "DEAD");
            channel->Write(res, sizeof(res));
            sem_post(&sync->toKid);

            // Wait for the kid to consume the result before the buffer is reused.
            if (!semWait(&sync->toHost, 5000)) {
                emit msg("Timeout - kid is not responding");
                gameOver = true;
                break;
            }

            const char* turn = wasAlive ? (kidAlive ? "hidden" : "caught")
                                        : (kidAlive ? "revived" : "dead");
            int hidden = (wasAlive && kidAlive) ? 1 : 0;
            int caught = (wasAlive && !kidAlive) ? 1 : 0;
            int deadTotal = kidAlive ? 0 : 1;
            emit msg(QString("R%1  Wolf %2  Kid %3  ->  %4 (%5)  | hidden %6, caught %7, dead %8  (streak %9)")
                         .arg(rounds).arg(wolfNum).arg(kidNum).arg(res)
                         .arg(turn).arg(hidden).arg(caught).arg(deadTotal).arg(deadStreak));

            if (deadStreak >= 2) {
                char end[MSG] = {};
                std::snprintf(end, sizeof(end), "END");
                channel->Write(end, sizeof(end));
                sem_post(&sync->toKid);
                emit msg("GAME OVER - all kids dead for 2 rounds");
                gameOver = true;
                break;
            }
            msleep(1500);
        }
    }
};

class HostWindow : public QMainWindow {
    Q_OBJECT
    QLabel* status;
    QLineEdit* input;
    QSemaphore inputReady;
    std::atomic<int> inputValue{0};
    Sync* sync = nullptr;
    std::unique_ptr<Conn> channel;
    Worker* worker = nullptr;
    pid_t childPid = -1;

    void kidBranch() {
#if CHANNEL_AFTER_FORK
        Channel local;
        kidLoop(&local, sync);
#else
        kidLoop(channel.get(), sync);
#endif
    }

public:
    explicit HostWindow(QWidget* parent = nullptr) : QMainWindow(parent) {
        status = new QLabel("Wolf & Kids - lobby. Enter wolf numbers to play.", this);
        status->setGeometry(10, 10, 760, 80);
        status->setWordWrap(true);
        input = new QLineEdit(this);
        input->setGeometry(10, 100, 320, 30);
        input->setPlaceholderText("Wolf number 1..100, Enter within 3 s");
        input->setValidator(new QIntValidator(1, 100, input));
        setWindowTitle("Lab2 Host - Wolf & Kids");
        resize(800, 200);

        sync = static_cast<Sync*>(mmap(nullptr, sizeof(Sync),
                                       PROT_READ | PROT_WRITE,
                                       MAP_SHARED | MAP_ANONYMOUS, -1, 0));
        if (sync == MAP_FAILED) {
            status->setText("fatal: shared memory for semaphores failed");
            sync = nullptr;
            return;
        }
        sem_init(&sync->toKid, 1, 0);
        sem_init(&sync->toHost, 1, 0);

#if !CHANNEL_AFTER_FORK
        channel.reset(new Channel());
#endif

        connect(input, &QLineEdit::returnPressed, this, [this]() {
            bool ok = false;
            int typed = input->text().toInt(&ok);
            if (ok && typed >= 1 && typed <= 100) {
                inputValue.store(typed);
                inputReady.release();
                input->clear();
            }
        });

        childPid = fork();
        if (childPid == 0) {
            // Give the kid its own PRNG stream; otherwise it inherits the
            // host's seeded state and produces the same numbers in lockstep.
            std::srand(static_cast<unsigned>(std::time(nullptr)) ^
                       static_cast<unsigned>(getpid()));
            kidBranch();
            _exit(0);
        } else if (childPid > 0) {
            logMsg("Host forked kid=" + std::to_string(childPid));
#if CHANNEL_AFTER_FORK
            channel.reset(new Channel());
#endif
            QTimer::singleShot(500, this, [this]() {
                worker = new Worker(channel.get(), sync, &inputReady, &inputValue, this);
                connect(worker, &Worker::msg, status, &QLabel::setText);
                worker->start();
            });
        } else {
            status->setText("fatal: fork() failed");
        }
    }

    ~HostWindow() override {
        if (childPid > 0) {
            kill(childPid, SIGTERM);
            waitpid(childPid, nullptr, 0);
        }
        if (worker) {
            if (!worker->wait(6000)) worker->terminate();
        }
        if (sync) {
            sem_destroy(&sync->toKid);
            sem_destroy(&sync->toHost);
            munmap(sync, sizeof(Sync));
        }
    }
};

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    HostWindow window;
    window.show();
    return app.exec();
}

#include "host.moc"
