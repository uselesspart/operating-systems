#include "conn.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <cstdio>
#include <ctime>
#include <poll.h>

static const char* FIFO_C2S = "/tmp/my_c2s";
static const char* FIFO_S2C = "/tmp/my_s2c";
static const char* SEM_NAME = "/my_fifo_sem";

class ConnFifo : public Conn {
public:
    ConnFifo(const std::string& id, bool create)
        : Conn(id, create), fd_rd(-1), fd_wr(-1) {
        sem = sem_open(SEM_NAME, O_CREAT, 0644, 1);
        if (create) {
            unlink(FIFO_C2S); unlink(FIFO_S2C);
            mkfifo(FIFO_C2S, 0666); mkfifo(FIFO_S2C, 0666);
        }
    }

    ~ConnFifo() override {
        if (fd_rd != -1) close(fd_rd);
        if (fd_wr != -1) close(fd_wr);
        if (m_create) { unlink(FIFO_C2S); unlink(FIFO_S2C); sem_unlink(SEM_NAME); }
        else sem_close(sem);
    }

    bool Handshake() override {
        if (m_create) {
            fd_rd = open(FIFO_C2S, O_RDONLY);
            fd_wr = open(FIFO_S2C, O_WRONLY);
            return fd_rd != -1 && fd_wr != -1;
        }
        pid_t host = std::stoi(m_id);
        if (kill(host, SIGUSR1) == -1) { perror("kill"); return false; }
        fd_wr = open(FIFO_C2S, O_WRONLY);
        fd_rd = open(FIFO_S2C, O_RDONLY);
        return fd_wr != -1 && fd_rd != -1;
    }

    bool Write(const void* buf, size_t count) override {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
        if (sem_timedwait(sem, &ts) == -1) return false; 
        ssize_t n = write(fd_wr, buf, count);
        sem_post(sem);
        return n == (ssize_t)count;
    }

    bool Read(void* buf, size_t count) override {
        struct pollfd pfd{fd_rd, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) return false; 
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
        if (sem_timedwait(sem, &ts) == -1) return false;
        ssize_t n = read(fd_rd, buf, count);
        sem_post(sem);
        return n > 0;
    }

private:
    int fd_rd, fd_wr;
    sem_t* sem;
};

Conn* createConn(const std::string& id, bool create) { return new ConnFifo(id, create); }