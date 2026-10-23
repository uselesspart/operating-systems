// Взаимодействие через именованные каналы (mkfifo) — TYPE_CODE: fifo.
// Конструктор только вычисляет имена; создание и открытие FIFO выполняется в Open(),
// то есть в каждом процессе уже после fork (как у несвязанных процессов).
// Файлы FIFO удаляет хост при закрытии.
#include <sys/stat.h>
#include <sys/types.h>

#include <cstring>
#include <stdexcept>

#include "conn.h"
#include "conn_util.h"
#include "log.h"

struct Conn::Impl {
    int id = 0;
    bool create = false;
    std::string base, c2h, h2c;
    int rfd = -1, wfd = -1;
};

namespace {
// open() с повторами, пока другая сторона не создала/не открыла FIFO
int open_wait(const std::string& path, int flags, long long deadline) {
    for (;;) {
        int fd = ::open(path.c_str(), flags | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0) return fd;
        if ((errno != ENOENT && errno != ENXIO) || connutil::now_ms() > deadline) return -1;
        usleep(20000);
    }
}
}  // namespace

Conn::Conn(int id, bool create, const std::string& spec) : p_(new Impl) {
    p_->id = id;
    p_->create = create;
    if (create) {
        p_->base = "/tmp/chat_fifo_" + std::to_string(getpid()) + "_" + std::to_string(id);
    } else {
        if (spec.empty()) throw std::runtime_error("empty fifo spec");
        p_->base = spec;
    }
    p_->c2h = p_->base + "_c2h";
    p_->h2c = p_->base + "_h2c";
}

Conn::~Conn() { Close(); }

bool Conn::Open() {
    const long long deadline = connutil::now_ms() + 5000;
    if (p_->create) {
        ::unlink(p_->c2h.c_str());
        ::unlink(p_->h2c.c_str());
        if (mkfifo(p_->c2h.c_str(), 0600) < 0 || mkfifo(p_->h2c.c_str(), 0600) < 0) {
            chatlog::line("fifo", std::string("mkfifo: ") + strerror(errno));
            return false;
        }
        p_->rfd = open_wait(p_->c2h, O_RDONLY, deadline);
        if (p_->rfd < 0) return false;
        p_->wfd = open_wait(p_->h2c, O_WRONLY, deadline);  // ждём, пока клиент откроет чтение
        return p_->wfd >= 0;
    }
    p_->rfd = open_wait(p_->h2c, O_RDONLY, deadline);
    if (p_->rfd < 0) return false;
    p_->wfd = open_wait(p_->c2h, O_WRONLY, deadline);
    return p_->wfd >= 0;
}

int Conn::Read(void* buf, size_t count, int timeout_ms) {
    return connutil::read_all(p_->rfd, buf, count, timeout_ms);
}

bool Conn::Write(const void* buf, size_t count, int timeout_ms) {
    return connutil::write_all(p_->wfd, buf, count, timeout_ms);
}

void Conn::Close() {
    connutil::close_fd(p_->rfd);
    connutil::close_fd(p_->wfd);
    if (p_->create) {  // родитель удаляет файлы именованных каналов
        ::unlink(p_->c2h.c_str());
        ::unlink(p_->h2c.c_str());
    }
}

std::string Conn::Spec() const { return p_->base; }

std::vector<int> Conn::ChildFds() const { return {}; }

void Conn::CloseChildEnds() {}

const char* Conn::TypeName() { return "fifo"; }
