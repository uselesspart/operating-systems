#pragma once

#include <unistd.h>

namespace application {

class UniqueFd {
public:
  explicit UniqueFd(int fd = -1) : fd_(fd) {}
  ~UniqueFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  UniqueFd(const UniqueFd &) = delete;
  UniqueFd &operator=(const UniqueFd &) = delete;
  int get() const { return fd_; }

private:
  int fd_;
};

} // namespace application
