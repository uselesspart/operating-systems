#include "connection.hpp"

#include <fcntl.h>
#include <sys/stat.h>

void Conn::Prepare(pid_t, bool create) {
    if (create) {
        for (const auto& name : {"c2h", "h2c"}) {
            if (mkfifo((directory_.path / name).c_str(), 0600) < 0) systemError("mkfifo");
        }
    }
    input_.reset(open((directory_.path / (create ? "c2h" : "h2c")).c_str(),
                      (create ? O_RDWR : O_RDONLY) | O_NONBLOCK | O_CLOEXEC));
    if (input_.get() < 0) systemError("open fifo");
    output_.reset(open((directory_.path / (create ? "h2c" : "c2h")).c_str(),
                       (create ? O_RDWR : O_WRONLY) | O_NONBLOCK | O_CLOEXEC));
    if (output_.get() < 0) systemError("open fifo");
}

void Conn::Accept() {}
