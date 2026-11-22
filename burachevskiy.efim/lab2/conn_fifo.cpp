#include "conn.hpp"
#include "system.hpp"

#include <fcntl.h>
#include <sys/stat.h>

Conn::Conn(bool host, const std::string& name, int, int) : host_(host), name_(name) {
    const auto up = name_ + "/up";
    const auto down = name_ + "/down";
    try {
        if (host_ && (mkfifo(up.c_str(), 0600) < 0 || mkfifo(down.c_str(), 0600) < 0)) fail("mkfifo");
        in_ = open((host_ ? up : down).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (in_ < 0) fail("open fifo");
        out_ = open((host_ ? down : up).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (out_ < 0) fail("open fifo");
    } catch (...) {
        if (in_ >= 0) close(in_);
        if (host_) { unlink(up.c_str()); unlink(down.c_str()); }
        throw;
    }
}
Conn::~Conn() {
    close(in_);
    close(out_);
    if (host_) { unlink((name_ + "/up").c_str()); unlink((name_ + "/down").c_str()); }
}
void Conn::Read(void* buf, std::size_t count) { transfer(in_, buf, count, false); }
void Conn::Write(const void* buf, std::size_t count) { transfer(out_, const_cast<void*>(buf), count, true); }
