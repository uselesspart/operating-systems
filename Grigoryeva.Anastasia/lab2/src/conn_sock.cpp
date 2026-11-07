// Тип взаимодействия "sock" - сокеты (локальные, AF_UNIX, SOCK_STREAM).
// Хост на каждого клиента создаёт слушающий сокет /tmp/wolf_<pid>_<id>.sock; клиент (получив id при
// знакомстве) подключается к нему. accept() хост выполняет при первой операции чтения/записи.
// Порядок операций задают глобальные семафоры.
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "conn.h"
#include "fd_io.h"
#include "sync_sem.h"

struct Conn::Impl {
    SemChannel sem;
    bool host;
    std::string path;
    UniqueFd listenFd, dataFd;
    std::string err;

    Impl(const ConnId& id, bool create) : sem(id, create), host(create), path("/tmp/" + ConnName(id) + ".sock") {
        try {
            struct sockaddr_un addr;
            std::memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            if (path.size() >= sizeof(addr.sun_path)) throw std::runtime_error("слишком длинный путь сокета");
            std::strcpy(addr.sun_path, path.c_str());

            if (create) {
                listenFd.reset(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
                if (!listenFd.valid()) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
                unlink(path.c_str());
                if (bind(listenFd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
                    throw std::runtime_error("bind " + path + ": " + std::strerror(errno));
                }
                chmod(path.c_str(), 0600);
                if (listen(listenFd.get(), 1) != 0) {
                    throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
                }
            } else {
                dataFd.reset(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
                if (!dataFd.valid()) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
                if (connect(dataFd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
                    throw std::runtime_error("connect " + path + ": " + std::strerror(errno));
                }
            }
        } catch (...) {
            if (host) unlink(path.c_str());
            throw;
        }
    }
    ~Impl() {
        if (host) unlink(path.c_str());
    }

    // Хост: дожидается подключения клиента (не дольше тайм-аута)
    bool EnsureConnected() {
        if (dataFd.valid()) return true;
        struct pollfd pfd = {listenFd.get(), POLLIN, 0};
        int r;
        do {
            r = poll(&pfd, 1, IoTimeoutMs());
        } while (r < 0 && errno == EINTR);
        if (r <= 0) {
            err = r == 0 ? "таймаут ожидания подключения клиента" : std::string("poll: ") + std::strerror(errno);
            return false;
        }
        int fd = accept4(listenFd.get(), nullptr, nullptr, SOCK_CLOEXEC);
        if (fd < 0) {
            err = std::string("accept: ") + std::strerror(errno);
            return false;
        }
        dataFd.reset(fd);
        listenFd.reset();
        unlink(path.c_str());
        return true;
    }
};

Conn::Conn(const ConnId& id, bool create) : impl_(new Impl(id, create)) {}
Conn::~Conn() = default;

const std::string& Conn::LastError() const { return impl_->err; }

bool Conn::Read(void* buf, size_t count) {
    Impl& d = *impl_;
    if (!d.EnsureConnected()) return false;
    if (!d.sem.WaitIncoming(IoTimeoutMs(), d.err)) return false;
    return ReadFdExact(d.dataFd.get(), buf, count, IoTimeoutMs(), d.err);
}

bool Conn::Write(const void* buf, size_t count) {
    Impl& d = *impl_;
    if (!d.EnsureConnected()) return false;
    if (!WriteFdAll(d.dataFd.get(), buf, count, IoTimeoutMs(), true, d.err)) return false;
    d.sem.PostOutgoing();
    return true;
}
