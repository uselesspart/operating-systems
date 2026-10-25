// Взаимодействие через сокеты — TYPE_CODE: sock.
// Для родственных процессов используется socketpair (AF_UNIX, SOCK_STREAM).
#include <sys/socket.h>
#include <sys/types.h>

#include <stdexcept>

#include "conn.h"
#include "conn_util.h"

struct Conn::Impl {
    int id = 0;
    bool create = false;
    int fd = -1;    // мой конец
    int cfd = -1;   // конец ребёнка (только на стороне хоста до fork)
};

Conn::Conn(int id, bool create, const std::string& spec) : p_(new Impl) {
    p_->id = id;
    p_->create = create;
    if (create) {
        int sv[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0)
            throw std::runtime_error("socketpair() failed");
        p_->fd = sv[0];
        p_->cfd = sv[1];
    } else {
        int fd = -1;
        if (sscanf(spec.c_str(), "%d", &fd) != 1 || fd < 0)
            throw std::runtime_error("bad socket spec: " + spec);
        p_->fd = fd;
    }
}

Conn::~Conn() { Close(); CloseChildEnds(); }

bool Conn::Open() { return connutil::set_nonblock(p_->fd); }

int Conn::Read(void* buf, size_t count, int timeout_ms) {
    return connutil::read_all(p_->fd, buf, count, timeout_ms);
}

bool Conn::Write(const void* buf, size_t count, int timeout_ms) {
    return connutil::write_all(p_->fd, buf, count, timeout_ms);
}

void Conn::Close() { connutil::close_fd(p_->fd); }

std::string Conn::Spec() const { return std::to_string(p_->cfd); }

std::vector<int> Conn::ChildFds() const { return {p_->cfd}; }

void Conn::CloseChildEnds() { connutil::close_fd(p_->cfd); }

const char* Conn::TypeName() { return "sock"; }
