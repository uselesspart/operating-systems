#include "conn.hpp"
#include "system.hpp"

#include <fcntl.h>
#include <mqueue.h>

namespace {
std::string queueName(const std::string& directory, const char* suffix) {
    return "/" + directory.substr(directory.rfind('/') + 1) + suffix;
}
}

Conn::Conn(bool host, const std::string& name, int, int) : host_(host), name_(name) {
    const auto up = queueName(name_, "-up");
    const auto down = queueName(name_, "-down");
    mq_attr attributes{};
    attributes.mq_maxmsg = 8;
    attributes.mq_msgsize = frameSize;
    const auto flags = O_NONBLOCK | O_CLOEXEC | (host_ ? O_CREAT | O_EXCL : 0);
    const auto& input = host_ ? up : down;
    const auto& output = host_ ? down : up;
    in_ = mq_open(input.c_str(), flags | O_RDONLY, 0600, &attributes);
    if (in_ < 0) fail("mq_open");
    out_ = mq_open(output.c_str(), flags | O_WRONLY, 0600, &attributes);
    if (out_ < 0) {
        const int error = errno;
        mq_close(in_);
        if (host_) mq_unlink(input.c_str());
        errno = error;
        fail("mq_open");
    }
}
Conn::~Conn() {
    mq_close(in_);
    mq_close(out_);
    if (host_) {
        mq_unlink(queueName(name_, "-up").c_str());
        mq_unlink(queueName(name_, "-down").c_str());
    }
}
void Conn::Read(void* buf, std::size_t count) {
    const auto deadline = Clock::now() + 5s;
    for (;;) {
        waitFd(in_, POLLIN, deadline);
        const auto n = mq_receive(in_, static_cast<char*>(buf), count, nullptr);
        if (n == static_cast<ssize_t>(count)) return;
        if (n >= 0) throw std::runtime_error("Неверный размер сообщения mq");
        if (errno != EAGAIN && errno != EINTR) fail("mq_receive");
    }
}
void Conn::Write(const void* buf, std::size_t count) {
    const auto deadline = Clock::now() + 5s;
    for (;;) {
        waitFd(out_, POLLOUT, deadline);
        if (mq_send(out_, static_cast<const char*>(buf), count, 0) == 0) return;
        if (errno != EAGAIN && errno != EINTR) fail("mq_send");
    }
}
