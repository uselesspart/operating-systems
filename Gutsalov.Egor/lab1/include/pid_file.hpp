#pragma once

#include "unique_fd.hpp"

#include <filesystem>

namespace application {

class PidFile {
public:
  explicit PidFile(const std::filesystem::path &path);
  ~PidFile();

  PidFile(const PidFile &) = delete;
  PidFile &operator=(const PidFile &) = delete;

private:
  UniqueFd fd_;
  bool owned_ = false;
};

} // namespace application
