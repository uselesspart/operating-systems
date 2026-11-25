#include "conn.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <semaphore.h>
#include <signal.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h> 
#include <poll.h>

static const char* SOCK_PATH = "/tmp/my_sock";
static const char* SEM_NAME  = "/my_sock_sem";

class ConnSock : public Conn {
public:
    ConnSock(const std::string& id, bool create)
        : Conn(id, create), fd(-1), listen_fd(-1), peer_pid(-1) {
        sem = sem_open(SEM_NAME, O_CREAT, 0644, 1);
        if (create) {
            unlink(SOCK_PATH);
            listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
            struct sockaddr_un addr{};
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
            bind(listen_fd, (sockaddr*)&addr, sizeof(addr));
            listen(listen_fd, 5);
        }
    }

    ~ConnSock() override {
        if (fd != -1) close(fd);
        if (listen_fd != -1) { close(listen_fd); unlink(SOCK_PATH); }
        if (m_create) sem_unlink(SEM_NAME);
        else sem_close(sem);
    }

    bool Handshake() override {
        if (m_create) {
            fd = accept(listen_fd, nullptr, nullptr);
            if (fd == -1) return false;
            struct ucred cred; socklen_t len = sizeof(cred);
            if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
                peer_pid = cred.pid;
            return true;
        }
        pid_t host = std::stoi(m_id);
        if (kill(host, SIGUSR1) == -1) { perror("kill"); return false; }
        fd = socket(AF_UNIX, SOCK_STREAM, 0);
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, SOCK_PATH, sizeof(addr.sun_path)-1);
        return connect(fd, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool Write(const void* buf, size_t count) override {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
        if (sem_timedwait(sem, &ts) == -1) return false;
        ssize_t n = send(fd, buf, count, MSG_NOSIGNAL);
        sem_post(sem);
        return n == (ssize_t)count;
    }

    bool Read(void* buf, size_t count) override {
        struct pollfd pfd{fd, POLLIN, 0};
        if (poll(&pfd, 1, 100) <= 0) return false; 
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts); ts.tv_sec += 5;
        if (sem_timedwait(sem, &ts) == -1) return false; 
        ssize_t n = recv(fd, buf, count, 0);
        sem_post(sem);
        return n > 0;
    }

    int GetPeerPid() const override { return peer_pid; }

private:
    int fd, listen_fd, peer_pid;
    sem_t* sem;
};

Conn* createConn(const std::string& id, bool create) { return new ConnSock(id, create); }