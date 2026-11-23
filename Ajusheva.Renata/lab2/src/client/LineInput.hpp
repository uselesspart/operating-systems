#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace chatlab {

class LineInput {
  public:
    explicit LineInput(int fd);

    std::vector<std::string> poll(std::chrono::milliseconds timeout);
    bool closed() const noexcept { return closed_; }

  private:
    void splitLines(std::vector<std::string>& lines);

    int fd_;
    std::string pending_;
    bool closed_ = false;
};

}
