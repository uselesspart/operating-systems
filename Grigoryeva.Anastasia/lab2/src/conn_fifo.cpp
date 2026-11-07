// Тип взаимодействия "fifo" - именованные каналы (mkfifo).
// На каждого клиента хост создаёт два FIFO в /tmp: *_h2c (хост -> клиент) и *_c2h (клиент -> хост),
// и удаляет их файлы при завершении работы с клиентом. Порядок операций задают глобальные семафоры.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "conn.h"
#include "fd_io.h"
#include "sync_sem.h"

namespace {
std::string FifoPath(const ConnId& id, const char* dir) { return "/tmp/" + ConnName(id) + "_" + dir + ".fifo"; }
}  // namespace

struct Conn::Impl {
    SemChannel sem;
    bool host;
    std::string pathH2C, pathC2H;
    UniqueFd rd, wr;
    std::string err;

    Impl(const ConnId& id, bool create)
        : sem(id, create), host(create), pathH2C(FifoPath(id, "h2c")), pathC2H(FifoPath(id, "c2h")) {
        try {
            if (create) {
                unlink(pathH2C.c_str());
                unlink(pathC2H.c_str());
                if (mkfifo(pathH2C.c_str(), 0600) != 0 || mkfifo(pathC2H.c_str(), 0600) != 0) {
                    throw std::runtime_error(std::string("mkfifo: ") + std::strerror(errno));
                }
                // O_RDWR на Linux не блокируется, даже если собеседник ещё не открыл FIFO
                wr.reset(open(pathH2C.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
                rd.reset(open(pathC2H.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC));
            } else {
                // O_NONBLOCK: если хоста нет, open вернёт ошибку, а не повиснет навсегда
                rd.reset(open(pathH2C.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
                wr.reset(open(pathC2H.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
            }
            if (!rd.valid() || !wr.valid()) {
                throw std::runtime_error(std::string("open fifo: ") + std::strerror(errno));
            }
        } catch (...) {
            RemoveFiles();
            throw;
        }
    }
    ~Impl() { RemoveFiles(); }

    void RemoveFiles() {
        if (!host) return;  // файлы удаляет только создатель - хост
        unlink(pathH2C.c_str());
        unlink(pathC2H.c_str());
    }
};

Conn::Conn(const ConnId& id, bool create) : impl_(new Impl(id, create)) {}
Conn::~Conn() = default;

const std::string& Conn::LastError() const { return impl_->err; }

bool Conn::Read(void* buf, size_t count) {
    Impl& d = *impl_;
    if (!d.sem.WaitIncoming(IoTimeoutMs(), d.err)) return false;
    return ReadFdExact(d.rd.get(), buf, count, IoTimeoutMs(), d.err);
}

bool Conn::Write(const void* buf, size_t count) {
    Impl& d = *impl_;
    if (!WriteFdAll(d.wr.get(), buf, count, IoTimeoutMs(), false, d.err)) return false;
    d.sem.PostOutgoing();
    return true;
}
