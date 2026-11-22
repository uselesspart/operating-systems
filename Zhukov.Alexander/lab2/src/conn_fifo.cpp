#include "conn.h"
#include "fd.h"
#include "stream.h"

#include <fcntl.h>
#include <limits.h>
#include <stdexcept>
#include <sys/stat.h>

static_assert(max_packet + 4 <= PIPE_BUF);

class FifoConn final : public Conn {
public:
    ~FifoConn() override { cleanup(); }
    std::string type() const override { return "fifo"; }
    bool writable() const override { return can_write(output.get()); }

    void open(const std::string& directory, bool create) override {
        dir.reset(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (dir.get() < 0) {
            system_error("Cannot open session directory");
        }
        host = create;
        try {
            if (host) {
                if (mkfifoat(dir.get(), "up", 0600) != 0) {
                    system_error("Cannot create FIFO up");
                }
                up_created = true;
                if (mkfifoat(dir.get(), "down", 0600) != 0) {
                    system_error("Cannot create FIFO down");
                }
                down_created = true;
                input = open_fifo("up", O_RDONLY);
            } else {
                input = open_fifo("down", O_RDONLY);
                output = open_fifo("up", O_WRONLY);
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }

    void accept() override {
        if (host) {
            output = open_fifo("down", O_WRONLY);
        }
    }

    void send(const Bytes& packet, const std::atomic_bool& stop) override {
        stream.send(output.get(), packet, stop, false);
    }

    ReadResult receive(Bytes& packet, const std::atomic_bool& stop, int wait_ms,
                       Deadline* deadline) override {
        return stream.receive(input.get(), packet, stop, wait_ms, deadline);
    }

private:
    Fd open_fifo(const char* name, int mode) {
        Fd fd(openat(dir.get(), name, mode | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
        if (fd.get() < 0) {
            system_error("Cannot open FIFO");
        }
        struct stat info{};
        if (fstat(fd.get(), &info) != 0) {
            system_error("Cannot check FIFO");
        }
        if (!S_ISFIFO(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0777) != 0600) {
            throw std::runtime_error("Invalid FIFO");
        }
        return fd;
    }

    void cleanup() noexcept {
        input.reset();
        output.reset();
        if (up_created) {
            unlinkat(dir.get(), "up", 0);
            up_created = false;
        }
        if (down_created) {
            unlinkat(dir.get(), "down", 0);
            down_created = false;
        }
    }

    Fd dir;
    Fd input;
    Fd output;
    bool host = false;
    bool up_created = false;
    bool down_created = false;
    Stream stream;
};

std::unique_ptr<Conn> make_connection() { return std::make_unique<FifoConn>(); }
