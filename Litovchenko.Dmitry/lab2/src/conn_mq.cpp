#include <fcntl.h>
#include <mqueue.h>
#include <time.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include "common.h"
#include "conn.h"

namespace {

constexpr long kMsgSize = 64;

std::string queueName(int id, const char* direction) {
    return "/wolf_" + std::to_string(id) + "_" + direction;
}

timespec deadline() {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += kTimeoutSec;
    return ts;
}

}

const char* Conn::Type() {
    return "mq";
}

Conn::Conn(int id, bool create) : id_(id), create_(create) {
    const std::string toGoat = queueName(id, "h2g");
    const std::string toWolf = queueName(id, "g2h");
    if (create) {
        mq_attr attr{};
        attr.mq_maxmsg = 4;
        attr.mq_msgsize = kMsgSize;
        mq_unlink(toGoat.c_str());
        mq_unlink(toWolf.c_str());
        wr_ = mq_open(toGoat.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600, &attr);
        rd_ = mq_open(toWolf.c_str(), O_RDONLY | O_CREAT | O_EXCL, 0600, &attr);
    } else {
        retryWithTimeout([&] {
            rd_ = mq_open(toGoat.c_str(), O_RDONLY);
            return rd_ >= 0 || errno != ENOENT;
        });
        if (rd_ >= 0) wr_ = mq_open(toWolf.c_str(), O_WRONLY);
    }
    if (rd_ < 0 || wr_ < 0) {
        std::string error = std::string("mq_open: ") + std::strerror(errno);
        closeAll();
        throw std::runtime_error(error);
    }
}

Conn::~Conn() {
    closeAll();
}

void Conn::closeAll() {
    if (rd_ >= 0) mq_close(rd_);
    if (wr_ >= 0) mq_close(wr_);
    rd_ = wr_ = -1;
    if (create_) {
        mq_unlink(queueName(id_, "h2g").c_str());
        mq_unlink(queueName(id_, "g2h").c_str());
    }
}

bool Conn::Read(void* buf, size_t count) {
    char msg[kMsgSize];
    timespec until = deadline();
    ssize_t n = mq_timedreceive(rd_, msg, sizeof(msg), nullptr, &until);
    if (n != static_cast<ssize_t>(count)) return false;
    std::memcpy(buf, msg, count);
    return true;
}

bool Conn::Write(const void* buf, size_t count) {
    if (count > static_cast<size_t>(kMsgSize)) return false;
    timespec until = deadline();
    return mq_timedsend(wr_, static_cast<const char*>(buf), count, 0, &until) == 0;
}
