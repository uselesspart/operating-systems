#include "conn/conn.hpp"

#include "link/Descriptor.hpp"
#include "link/FdOps.hpp"
#include "link/Timeouts.hpp"
#include "util/Errors.hpp"

#include <cerrno>
#include <string>

#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

namespace chatlab {

namespace {

constexpr int kSlotBase = 600;

struct SlotFds {
    int guestReads;
    int guestWrites;
};

SlotFds fdsFor(int slot) {
    return {kSlotBase + 2 * slot, kSlotBase + 2 * slot + 1};
}

void allowDescriptor(int fd) {
    rlimit limit{};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0) {
        raiseErrno("getrlimit");
    }
    const rlim_t wanted = static_cast<rlim_t>(fd) + 1;
    if (limit.rlim_cur >= wanted) {
        return;
    }
    if (limit.rlim_max != RLIM_INFINITY && limit.rlim_max < wanted) {
        raiseErrno("too many clients for the descriptor limit", EMFILE);
    }
    limit.rlim_cur = wanted;
    if (::setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        raiseErrno("setrlimit");
    }
}

Descriptor pinTo(Descriptor source, int target) {
    if (::fcntl(target, F_GETFD) != -1) {
        raiseErrno("descriptor " + std::to_string(target) + " is busy", EBUSY);
    }
    if (::dup3(source.get(), target, O_CLOEXEC) < 0) {
        raiseErrno("dup3");
    }
    return Descriptor(target);
}

Descriptor openFromHost(pid_t host, int fd, int mode) {
    const std::string path = "/proc/" + std::to_string(host) + "/fd/" + std::to_string(fd);
    Descriptor result(::open(path.c_str(), mode | O_NONBLOCK | O_CLOEXEC));
    if (!result.isOpen()) {
        raiseErrno("open " + path);
    }
    fdops::setBlocking(result.get());
    return result;
}

}

struct Conn::State {
    Descriptor input;
    Descriptor output;
    Descriptor parkedRead;
    Descriptor parkedWrite;

    void releaseParked() {
        parkedRead.reset();
        parkedWrite.reset();
    }
};

Conn::Conn(const ConnKey& key, bool create) : state_(std::make_unique<State>()) {
    const SlotFds fds = fdsFor(key.slot);
    if (!create) {
        state_->input = openFromHost(key.host, fds.guestReads, O_RDONLY);
        state_->output = openFromHost(key.host, fds.guestWrites, O_WRONLY);
        return;
    }

    allowDescriptor(fds.guestWrites);
    int downward[2];
    if (::pipe2(downward, O_CLOEXEC) != 0) {
        raiseErrno("pipe2");
    }
    Descriptor downRead(downward[0]);
    state_->output.reset(downward[1]);

    int upward[2];
    if (::pipe2(upward, O_CLOEXEC) != 0) {
        raiseErrno("pipe2");
    }
    state_->input.reset(upward[0]);
    Descriptor upWrite(upward[1]);

    state_->parkedRead = pinTo(std::move(downRead), fds.guestReads);
    state_->parkedWrite = pinTo(std::move(upWrite), fds.guestWrites);
}

Conn::~Conn() = default;

bool Conn::Read(void* buf, std::size_t count) {
    if (state_->parkedRead.isOpen() && !fdops::waitReadable(state_->input.get(), kWaitLimit)) {
        return false;
    }
    if (!fdops::readFull(state_->input.get(), buf, count)) {
        return false;
    }
    state_->releaseParked();
    return true;
}

bool Conn::Write(const void* buf, std::size_t count) {
    return fdops::writeFull(state_->output.get(), buf, count);
}

const char* Conn::Kind() {
    return "pipe";
}

}
