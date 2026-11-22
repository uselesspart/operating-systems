#include "conn.h"
#include "fd.h"
#include "stream.h"

#include <algorithm>
#include <cstddef>
#include <fcntl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

class SocketConn final : public Conn {
public:
    ~SocketConn() override { cleanup(); }
    std::string type() const override { return "sock"; }
    bool writable() const override { return can_write(fd.get()); }

    void open(const std::string& directory, bool create) override {
        auto path = directory + "/socket";
        sockaddr_un address{};
        if (path.size() >= sizeof(address.sun_path)) {
            throw std::runtime_error("Socket path too long");
        }
        address.sun_family = AF_UNIX;
        std::copy(path.begin(), path.end(), address.sun_path);
        auto size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
        dir.reset(::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (dir.get() < 0) {
            system_error("Cannot open session directory");
        }
        try {
            Fd socket(::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
            if (socket.get() < 0) {
                system_error("Cannot create socket");
            }
            if (create) {
                if (bind(socket.get(), reinterpret_cast<sockaddr*>(&address), size) != 0) {
                    system_error("Cannot bind socket");
                }
                if (fstatat(dir.get(), "socket", &created, AT_SYMLINK_NOFOLLOW) != 0) {
                    system_error("Cannot check socket");
                }
                owner = true;
                if (chmod(path.c_str(), 0600) != 0 || listen(socket.get(), 1) != 0) {
                    system_error("Cannot listen on socket");
                }
                listener = std::move(socket);
            } else {
                struct stat info{};
                if (fstatat(dir.get(), "socket", &info, AT_SYMLINK_NOFOLLOW) != 0) {
                    system_error("Cannot check socket");
                }
                if (!S_ISSOCK(info.st_mode) || info.st_uid != getuid() ||
                    (info.st_mode & 0777) != 0600) {
                    throw std::runtime_error("Invalid socket");
                }
                if (::connect(socket.get(), reinterpret_cast<sockaddr*>(&address), size) != 0) {
                    system_error("Cannot connect socket");
                }
                fd = std::move(socket);
                check_peer();
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }

    void accept() override {
        fd.reset(accept4(listener.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (fd.get() < 0) {
            system_error("Cannot accept socket");
        }
        check_peer();
        listener.reset();
    }

    pid_t peer_pid() const override { return peer; }

    void send(const Bytes& packet, const std::atomic_bool& stop) override {
        stream.send(fd.get(), packet, stop, true);
    }

    ReadResult receive(Bytes& packet, const std::atomic_bool& stop, int wait_ms,
                       Deadline* deadline) override {
        return stream.receive(fd.get(), packet, stop, wait_ms, deadline);
    }

private:
    void check_peer() {
        ucred info{};
        socklen_t size = sizeof(info);
        if (getsockopt(fd.get(), SOL_SOCKET, SO_PEERCRED, &info, &size) != 0) {
            system_error("Cannot check peer");
        }
        if (size != sizeof(info) || info.uid != getuid() || info.pid <= 0) {
            throw std::runtime_error("Invalid socket peer");
        }
        peer = info.pid;
    }

    void cleanup() noexcept {
        fd.reset();
        listener.reset();
        if (owner) {
            struct stat info{};
            if (fstatat(dir.get(), "socket", &info, AT_SYMLINK_NOFOLLOW) == 0 &&
                S_ISSOCK(info.st_mode) && info.st_dev == created.st_dev &&
                info.st_ino == created.st_ino) {
                unlinkat(dir.get(), "socket", 0);
            }
            owner = false;
        }
    }

    Fd dir;
    Fd listener;
    Fd fd;
    Stream stream;
    struct stat created{};
    bool owner = false;
    pid_t peer = 0;
};

std::unique_ptr<Conn> make_connection() { return std::make_unique<SocketConn>(); }
