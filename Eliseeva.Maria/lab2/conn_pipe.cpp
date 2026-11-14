// Взаимодействие через анонимные программные каналы (pipe) — TYPE_CODE: pipe.
// Для каждого клиента создаются два канала: клиент->хост и хост->клиент.
#include <stdexcept>

#include "conn.h"
#include "conn_util.h"

struct Conn::Impl {
    int id = 0;
    bool create = false;
    int rfd = -1, wfd = -1;      // мои концы
    int cfd_r = -1, cfd_w = -1;  // концы ребёнка (только на стороне хоста до fork)
};

Conn::Conn(int id, bool create, const std::string& spec) : p_(new Impl) {
    p_->id = id;
    p_->create = create;
    if (create) {
        int c2h[2], h2c[2];
        if (pipe2(c2h, O_CLOEXEC) < 0) throw std::runtime_error("pipe() failed");
        if (pipe2(h2c, O_CLOEXEC) < 0) {
            ::close(c2h[0]);
            ::close(c2h[1]);
            throw std::runtime_error("pipe() failed");
        }
        p_->rfd = c2h[0];
        p_->wfd = h2c[1];
        p_->cfd_w = c2h[1];
        p_->cfd_r = h2c[0];
    } else {
        int w = -1, r = -1;
        if (sscanf(spec.c_str(), "%d %d", &w, &r) != 2 || w < 0 || r < 0)
            throw std::runtime_error("bad pipe spec: " + spec);
        p_->wfd = w;
        p_->rfd = r;
    }
}

Conn::~Conn() { Close(); CloseChildEnds(); }

bool Conn::Open() {
    return connutil::set_nonblock(p_->rfd) && connutil::set_nonblock(p_->wfd);
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
}

std::string Conn::Spec() const {
    return std::to_string(p_->cfd_w) + " " + std::to_string(p_->cfd_r);
}

std::vector<int> Conn::ChildFds() const { return {p_->cfd_w, p_->cfd_r}; }

void Conn::CloseChildEnds() {
    connutil::close_fd(p_->cfd_w);
    connutil::close_fd(p_->cfd_r);
}

const char* Conn::TypeName() { return "pipe"; }
