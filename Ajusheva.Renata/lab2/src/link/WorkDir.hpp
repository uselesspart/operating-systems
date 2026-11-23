#pragma once

#include <string>

#include <sys/types.h>

namespace chatlab {

class WorkDir {
  public:
    explicit WorkDir(pid_t host);
    ~WorkDir();

    WorkDir(const WorkDir&) = delete;
    WorkDir& operator=(const WorkDir&) = delete;

    const std::string& path() const noexcept { return path_; }

    static int sweepLeftovers();

  private:
    std::string path_;
};

}
