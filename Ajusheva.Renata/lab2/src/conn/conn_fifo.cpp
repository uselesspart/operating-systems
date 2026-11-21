#include "conn/conn.hpp"

#include "link/Descriptor.hpp"
#include "link/FdOps.hpp"
#include "link/Names.hpp"
#include "link/Timeouts.hpp"
#include "util/Errors.hpp"

#include <cerrno>
#include <chrono>
#include <string>
#include <thread>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace chatlab {

namespace {

constexpr auto kOpenRetry = std::chrono::milliseconds(25);

void makeFifo(const std::string& path) {
    ::unlink(path.c_str());
    if (::mkfifo(path.c_str(), 0600) != 0) {
        raiseErrno("mkfifo " + path);
    }
}

Descriptor openEnd(const std::string& path, int mode) {
    Descriptor fd(::open(path.c_str(), mode | O_NONBLOCK | O_CLOEXEC));
    if (!fd.isOpen()) {
        raiseErrno("open " + path);
    }
    return fd;
}

Descriptor openWriterWhenReady(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + kWaitLimit;
    while (true) {
        Descriptor fd(::open(path.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
        if (fd.isOpen() || errno != ENXIO || std::chrono::steady_clock::now() >= deadline) {
            return fd;
        }
        std::this_thread::sleep_for(kOpenRetry);
    }
}

}

struct Conn::State {
    bool owner = false;
    std::string readPath;
    std::string writePath;
    Descriptor input;
    Descriptor output;

    bool ensureOutput() {
        if (output.isOpen()) {
            return true;
        }
        output = openWriterWhenReady(writePath);
        if (!output.isOpen()) {
            return false;
        }
        fdops::setBlocking(output.get());
        return true;
    }
};

Conn::Conn(const ConnKey& key, bool create) : state_(std::make_unique<State>()) {
    const std::string up = names::fifoPath(key, names::kUp);
    const std::string down = names::fifoPath(key, names::kDown);
    state_->owner = create;
    state_->readPath = create ? up : down;
    state_->writePath = create ? down : up;

    if (create) {
        makeFifo(up);
        makeFifo(down);
        state_->input = openEnd(state_->readPath, O_RDONLY);
        fdops::setBlocking(state_->input.get());
        return;
    }

    state_->output = openEnd(state_->writePath, O_WRONLY);
    fdops::setBlocking(state_->output.get());
    state_->input = openEnd(state_->readPath, O_RDONLY);
    fdops::setBlocking(state_->input.get());
}

Conn::~Conn() {
    if (state_->owner) {
        ::unlink(state_->readPath.c_str());
        ::unlink(state_->writePath.c_str());
    }
}

bool Conn::Read(void* buf, std::size_t count) {
    return fdops::readFull(state_->input.get(), buf, count);
}

bool Conn::Write(const void* buf, std::size_t count) {
    return state_->ensureOutput() && fdops::writeFull(state_->output.get(), buf, count);
}

const char* Conn::Kind() {
    return "fifo";
}

}
