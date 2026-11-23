#include "connection.hpp"

#include <fcntl.h>
#include <unistd.h>

void Conn::Prepare(pid_t host, bool create) {
    if (create) {
        for (const auto& name : {"c2h", "h2c"}) {
            int ends[2];
            if (pipe2(ends, O_NONBLOCK | O_CLOEXEC) < 0) systemError("pipe2");
            Fd readEnd(ends[0]);
            Fd writeEnd(ends[1]);
            const bool incoming = std::string(name) == "c2h";
            const int shared = incoming ? writeEnd.get() : readEnd.get();
            const auto target = std::filesystem::path("/proc") / std::to_string(host)
                / "fd" / std::to_string(shared);
            std::filesystem::create_symlink(target, directory_.path / name);
            if (incoming) {
                input_ = std::move(readEnd);
                pending_.push_back(std::move(writeEnd));
            } else {
                output_ = std::move(writeEnd);
                pending_.push_back(std::move(readEnd));
            }
        }
    } else {
        input_.reset(open((directory_.path / "h2c").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
        if (input_.get() < 0) systemError("open pipe");
        output_.reset(open((directory_.path / "c2h").c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
        if (output_.get() < 0) systemError("open pipe");
    }
}

void Conn::Accept() {}
