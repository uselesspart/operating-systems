#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "common.h"
#include "conn.h"

namespace {

std::string fifoPath(int id, const char* direction) {
    return "/tmp/wolf_" + std::to_string(id) + "_" + direction + ".fifo";
}

int openWithTimeout(const std::string& path, int flags) {
    int fd = -1;
    retryWithTimeout([&] {
        fd = open(path.c_str(), flags | O_NONBLOCK | O_CLOEXEC);
        return fd >= 0 || (errno != ENOENT && errno != ENXIO);
    });
    if (fd >= 0) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    return fd;
}

}

const char* Conn::Type() {
    return "fifo";
}

Conn::Conn(int id, bool create) : id_(id), create_(create) {
    const std::string toGoat = fifoPath(id, "h2g");
    const std::string toWolf = fifoPath(id, "g2h");
    if (create) {
        unlink(toGoat.c_str());
        unlink(toWolf.c_str());
        if (mkfifo(toGoat.c_str(), 0600) < 0 || mkfifo(toWolf.c_str(), 0600) < 0) {
            std::string error = std::string("mkfifo: ") + std::strerror(errno);
            closeAll();
            throw std::runtime_error(error);
        }
        wr_ = openWithTimeout(toGoat, O_WRONLY);
        if (wr_ >= 0) rd_ = openWithTimeout(toWolf, O_RDONLY);
    } else {
        rd_ = openWithTimeout(toGoat, O_RDONLY);
        if (rd_ >= 0) wr_ = openWithTimeout(toWolf, O_WRONLY);
    }
    if (rd_ < 0 || wr_ < 0) {
        std::string error = std::string("fifo open: ") + std::strerror(errno);
        closeAll();
        throw std::runtime_error(error);
    }
}

Conn::~Conn() {
    closeAll();
}

void Conn::closeAll() {
    if (rd_ >= 0) close(rd_);
    if (wr_ >= 0) close(wr_);
    rd_ = wr_ = -1;
    if (create_) {
        unlink(fifoPath(id_, "h2g").c_str());
        unlink(fifoPath(id_, "g2h").c_str());
    }
}

bool Conn::Read(void* buf, size_t count) {
    return readAll(rd_, buf, count);
}

bool Conn::Write(const void* buf, size_t count) {
    return writeAll(wr_, buf, count);
}
