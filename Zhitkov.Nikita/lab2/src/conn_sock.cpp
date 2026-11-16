#include "ConnSock.h"
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>

// Создаёт пару соединённых сокетов до fork. Оба fd кладутся в общую память, каждый процесс после fork забирает свой конец и закрывает чужой конец в своей (унаследованной) таблице дескрипторов
void ConnSock::preFork(SharedSems* sems) {
    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sems->sock_fds) != 0) {
        throw std::runtime_error(std::string("socketpair: ") + std::strerror(errno));
    }
}

ConnSock::ConnSock(int fd) : fd_(fd) {
    // Таймауты на send/recv: если пир молчит или умер, не висим вечно
    struct timeval tv;
    tv.tv_sec  = 5;
    tv.tv_usec = 0;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

ConnSock::~ConnSock() {
    if (fd_ >= 0) close(fd_);
}

bool ConnSock::Write(const void* buf, size_t count) {
    // MSG_NOSIGNAL - не получить SIGPIPE, если пир закрыл соединение
    return send(fd_, buf, count, MSG_NOSIGNAL) == static_cast<ssize_t>(count);
}

bool ConnSock::Read(void* buf, size_t count) {
    return recv(fd_, buf, count, 0) == static_cast<ssize_t>(count);
}

std::unique_ptr<ConnSock> ConnSock::createHost(SharedSems* sems) {
    if (sems->sock_fds[0] < 0) {
        throw std::runtime_error("ConnSock: preFork not called");
    }
    // Забираем свой конец. Чужой закрываем толкьо в этой копии fd-таблицы -
    // в sems->sock_fds ничего не пишем, иначе сломаем ребёнка, если он ещё не успел вызвать createClient()
    int fd = sems->sock_fds[0];
    close(sems->sock_fds[1]);
    return std::make_unique<ConnSock>(fd);
}

std::unique_ptr<ConnSock> ConnSock::createClient(SharedSems* sems) {
    if (sems->sock_fds[1] < 0) {
        throw std::runtime_error("ConnSock: preFork not called");
    }
    int fd = sems->sock_fds[1];
    close(sems->sock_fds[0]);
    return std::make_unique<ConnSock>(fd);
}